import SwiftUI
import Security
import UniformTypeIdentifiers

private enum TokenStore {
    static let service = "AltForgeRemoteConnection"
    static func read() -> String {
        let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service, kSecReturnData as String: true]
        var value: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &value) == errSecSuccess,
              let data = value as? Data else { return "" }
        return String(data: data, encoding: .utf8) ?? ""
    }
    static func write(_ token: String) throws {
        let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service]
        SecItemDelete(query as CFDictionary)
        var record = query
        record[kSecValueData as String] = Data(token.utf8)
        record[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
        guard SecItemAdd(record as CFDictionary, nil) == errSecSuccess else { throw ClientError.storage }
    }
}
private enum ClientError: Error { case address, storage, rejected, response }
private struct AppEntry: Decodable, Identifiable { let id: String; let name: String }
private struct InstallJob: Decodable { let id: UInt64; let state: String }
private struct ConnectionFile: Decodable { let url: String; let token: String }

// No redirects: an Authorization header must never follow another host.
private final class SessionDelegate: NSObject, URLSessionTaskDelegate {
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
}
@MainActor
private final class Client: ObservableObject {
    @Published var address = UserDefaults.standard.string(forKey: "server") ?? ""
    @Published var token = TokenStore.read()
    @Published var apps: [AppEntry] = []
    @Published var status = "VPNを接続してから、接続設定を登録してください。"
    @Published var busy = false
    @Published var working = false
    private let session = URLSession(configuration: .ephemeral, delegate: SessionDelegate(), delegateQueue: nil)
    private func endpoint(_ path: String) throws -> URL {
        try remoteEndpoint(address: address, token: token, path: path)
    }
    private func request<T: Decodable>(_ path: String, body: Data? = nil, key: UInt64? = nil) async throws -> T {
        var req = URLRequest(url: try endpoint(path))
        req.timeoutInterval = 15
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        if let body {
            req.httpMethod = "POST"; req.httpBody = body
            req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        if let key { req.setValue(String(key), forHTTPHeaderField: "X-Request-ID") }
        let (data, response) = try await session.data(for: req)
        guard let http = response as? HTTPURLResponse, http.statusCode == 200 else { throw ClientError.rejected }
        guard data.count < 1024 * 1024 else { throw ClientError.response }
        return try JSONDecoder().decode(T.self, from: data)
    }
    func connect() async {
        guard !working else { return }
        working = true; defer { working = false }
        do {
            _ = try endpoint("/api/apps")
            try TokenStore.write(token)
            UserDefaults.standard.set(address, forKey: "server")
            apps = try await request("/api/apps")
            let job: InstallJob = try await request("/api/status")
            show(job)
            if job.id == 0 { status = "PCに接続しました。インストールするアプリを選んでください。" }
        } catch { status = "接続できません。両方のVPN、PCの起動、接続設定を確認してください。" }
    }
    func install(_ app: AppEntry) async {
        guard !busy && !working else { return }
        busy = true; working = true
        defer { working = false }
        do {
            let key = UInt64.random(in: 1...UInt64.max)
            let data = try JSONEncoder().encode(["id":app.id])
            let job: InstallJob = try await request("/api/install", body:data, key:key)
            show(job)
            await poll(job.id)
        } catch {
            // A lost response does not mean installation failed. Reconnect and
            // inspect native status before allowing another request.
            status = "結果を確認できません。「接続・状態を確認」でPCの状態を確認してください。"
        }
    }
    func refresh() async {
        do {
            let job: InstallJob = try await request("/api/status")
            show(job)
            if busy && job.state != "unknown" { await poll(job.id) }
        } catch { status = "PCと通信できません。VPN接続を確認してください。" }
    }
    private func poll(_ id: UInt64) async {
        for _ in 0..<900 {
            if !busy { return }
            do {
                try await Task.sleep(for: .seconds(2))
                let job: InstallJob = try await request("/api/status")
                guard job.id == id else { status = "別の処理が開始されています。状態を再確認してください。"; return }
                show(job)
                if job.state == "unknown" { return }
            } catch { status = "通信が途切れました。状態確認から結果を確認してください。"; return }
        }
    }
    private func show(_ job: InstallJob) {
        busy = ["queued","installing","awaiting-apple-id","unknown"].contains(job.state)
        switch job.state {
        case "queued", "installing": status = "インストール中です。VPNを接続したままお待ちください。"
        case "succeeded": status = "iPhoneへのインストールが完了しました。ホーム画面でアプリを開いてください。"
        case "failed": status = "インストールできませんでした。VPNとPC側の認証・端末登録を確認してください。"
        case "awaiting-apple-id": status = "PC側でApple IDの確認が必要です。"
        case "unknown": status = "完了を確認できません。重複を避けるためPCで状態を確認してください。"
        default: break
        }
    }
    func importSettings(_ url: URL) {
        let granted = url.startAccessingSecurityScopedResource()
        defer { if granted { url.stopAccessingSecurityScopedResource() } }
        do {
            let data = try Data(contentsOf: url)
            guard data.count < 4096 else { throw ClientError.response }
            let config = try JSONDecoder().decode(ConnectionFile.self, from:data)
            address = config.url; token = config.token
            _ = try endpoint("/api/apps")
            status = "設定を読み込みました。接続を確認してください。"
        } catch { status = "接続設定ファイルを読み込めませんでした。" }
    }
    func importQRCode(_ url: URL) {
        guard let connection = remoteConnection(from:url) else {
            status = "接続用QRコードを読み込めませんでした。"; return
        }
        address = connection.address; token = connection.token
        status = "QRコードから設定を読み込みました。「接続・状態を確認」を押してください。"
    }
}
@main
struct AltForgeRemoteApp: App {
    @StateObject private var client = Client()
    @State private var importing = false
    var body: some Scene {
        WindowGroup {
            NavigationStack {
                Form {
                    Section("接続設定") {
                        TextField("PCのVPNアドレス（http://…:49321）",text:$client.address)
                            .textInputAutocapitalization(.never).autocorrectionDisabled().keyboardType(.URL)
                        SecureField("接続キー",text:$client.token).textInputAutocapitalization(.never).autocorrectionDisabled()
                        Button("設定ファイルを読み込む") { importing = true }
                        Button("接続・状態を確認") { Task { await client.connect(); await client.refresh() } }.disabled(client.working)
                    }
                    Section("状態") { Text(client.status) }
                    Section("PCで許可したアプリ") {
                        if client.apps.isEmpty { Text("PCの指定フォルダーにIPAを置き、接続を確認してください。").foregroundStyle(.secondary) }
                        ForEach(client.apps) { app in
                            Button { Task { await client.install(app) } } label: {
                                HStack { Text(app.name).lineLimit(2); Spacer(); Text("インストール") }
                            }.disabled(client.busy || client.working)
                        }
                    }
                    Section { Text("PCを起動したままにし、PCとiPhoneを同じVPNに接続してください。Apple IDと署名処理はPC側で管理します。") }
                }
                .navigationTitle("AltForge Remote")
                .onOpenURL { client.importQRCode($0) }
                .fileImporter(isPresented:$importing,allowedContentTypes:[.json]) { result in
                    if case .success(let url) = result { client.importSettings(url) }
                }
            }
        }
    }
}
