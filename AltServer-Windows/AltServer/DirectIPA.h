#pragma once

// This bridge passes only an IPA path. Signing and installation remain inside
// AltServerApp::InstallApplication; no UI automation or alternate signer is used.
#include <windows.h>
#include <wincred.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <optional>
#include <set>
#include <cwctype>
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Comctl32.lib")

namespace DirectIPA {
constexpr ULONG_PTR Protocol = 0x49504131; // IPA1
constexpr UINT InstallMessage = WM_APP + 81;
constexpr UINT CompleteMessage = WM_APP + 82;
inline bool busy = false;
inline bool loginForDirectIPA = false;
inline std::optional<std::wstring> pending;
inline std::wstring endpoint;
inline HANDLE singletonMutex = nullptr;

inline void State(const char* stage) {
    // Local diagnostic state contains only fixed stage names, no account,
    // device, IPA path or Apple response data.
    wchar_t folder[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, folder))) return;
    auto directory = std::filesystem::path(folder) / L"AltServerDirectIPA";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (!error) std::ofstream(directory / L"state.txt", std::ios::trunc) << stage;
}

inline void Alert(const wchar_t* message) {
    MessageBoxW(nullptr, message, L"AltServer Direct IPA", MB_OK | MB_ICONINFORMATION);
}

inline std::wstring UserSID(DWORD processID) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processID);
    if (!process) return {};
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) { CloseHandle(process); return {}; }
    DWORD length = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &length);
    std::vector<BYTE> data(length);
    std::wstring result;
    if (length && GetTokenInformation(token, TokenUser, data.data(), length, &length)) {
        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &text)) {
            result = text;
            LocalFree(text);
        }
    }
    CloseHandle(token);
    CloseHandle(process);
    return result;
}

inline bool ValidPath(const std::wstring& path) {
    // Shell entry accepts local absolute files only; reject directories, pipes,
    // device namespace and network paths before invoking the archive reader.
    if (path.empty() || path.size() > 32760 || path.find(L'\0') != std::wstring::npos ||
        path.size() < 3 || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/')) return false;
    std::filesystem::path file(path);
    auto extension = file.extension().wstring();
    for (auto& c : extension) c = static_cast<wchar_t>(towlower(c));
    if (extension != L".ipa") return false;
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error) || error) return false;
    auto size = std::filesystem::file_size(file, error);
    return !error && size > 0 && size <= 4ULL * 1024 * 1024 * 1024;
}

inline std::wstring CredentialTarget(const std::string& deviceID) {
    return L"AltServerDirectIPA/" + WideStringFromString(deviceID);
}

struct Credentials {
    std::wstring user;
    std::wstring password;
    Credentials() = default;
    Credentials(const Credentials&) = delete;
    Credentials& operator=(const Credentials&) = delete;
    ~Credentials() { Clear(); }
    void Clear() {
        if (!password.empty()) SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
        password.clear();
    }
};

inline bool ReadCredentials(const std::string& deviceID, Credentials& result) {
    PCREDENTIALW credential = nullptr;
    auto target = CredentialTarget(deviceID);
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return false;
    bool valid = credential->UserName && credential->CredentialBlob &&
        credential->CredentialBlobSize && credential->CredentialBlobSize % sizeof(wchar_t) == 0;
    if (valid) {
        result.user = credential->UserName;
        result.password.assign(reinterpret_cast<wchar_t*>(credential->CredentialBlob),
            credential->CredentialBlobSize / sizeof(wchar_t));
    }
    if (credential->CredentialBlob && credential->CredentialBlobSize)
        SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return valid && !result.user.empty() && !result.password.empty();
}

inline bool SaveCredentials(const std::string& deviceID, const wchar_t* user, const wchar_t* password) {
    auto target = CredentialTarget(deviceID);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.UserName = const_cast<wchar_t*>(user);
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(password));
    credential.CredentialBlobSize = static_cast<DWORD>(wcslen(password) * sizeof(wchar_t));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE; // Persists for this Windows user.
    return CredWriteW(&credential, 0) != FALSE;
}

inline void ForgetCredentials() {
    DWORD count = 0;
    PCREDENTIALW* credentials = nullptr;
    if (!CredEnumerateW(L"AltServerDirectIPA/*", 0, &count, &credentials)) {
        Alert(L"保存済みのApple IDはありません。");
        return;
    }
    bool success = true;
    for (DWORD i = 0; i < count; ++i)
        if (!CredDeleteW(credentials[i]->TargetName, CRED_TYPE_GENERIC, 0)) success = false;
    CredFree(credentials);
    Alert(success ? L"保存したApple IDとパスワードを削除しました。" : L"一部の認証情報を削除できませんでした。");
}

inline std::vector<std::shared_ptr<Device>> CandidateDevices(const std::vector<std::shared_ptr<Device>>& all) {
    std::vector<std::shared_ptr<Device>> devices;
    std::set<std::string> identifiers;
    for (auto& device : all) {
        if (device && (device->type() & (Device::iPhone | Device::iPad)) &&
            identifiers.insert(device->identifier()).second) devices.push_back(device);
    }
    return devices;
}

inline std::shared_ptr<Device> SelectDevice(HWND owner) {
    auto devices = CandidateDevices(DeviceManager::instance()->availableDevices());
    if (devices.empty()) { Alert(L"接続中のiPhone／iPadが見つかりません。端末を接続し、このPCを信頼してください。"); return nullptr; }
    if (devices.size() == 1) return devices[0];
    std::vector<std::wstring> labels;
    for (size_t i = 0; i < devices.size(); ++i)
        labels.push_back(WideStringFromString(devices[i]->name()) + L" (" + std::to_wstring(i + 1) + L")");
    std::vector<TASKDIALOG_BUTTON> buttons;
    for (size_t i = 0; i < labels.size(); ++i)
        buttons.push_back({1000 + static_cast<int>(i), labels[i].c_str()});
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    // The server's owner is a message-only HWND. Use a desktop dialog so the
    // device choice is visible and targetable even though the server is hidden.
    config.hwndParent = nullptr;
    config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = L"AltServer Direct IPA";
    config.pszMainInstruction = L"インストールする端末を選択";
    config.cButtons = static_cast<UINT>(buttons.size());
    config.pButtons = buttons.data();
    config.nDefaultButton = IDCANCEL;
    int choice = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&config, &choice, nullptr, nullptr))) {
        Alert(L"端末の選択画面を表示できませんでした。"); return nullptr;
    }
    if (choice < 1000 || static_cast<size_t>(choice - 1000) >= devices.size()) return nullptr;
    return devices[choice - 1000];
}

inline bool AcceptRequest(HWND window, WPARAM sender, LPARAM payload) {
    if (busy || pending) return false;
    DWORD processID = 0;
    GetWindowThreadProcessId(reinterpret_cast<HWND>(sender), &processID);
    if (!processID || UserSID(processID) != UserSID(GetCurrentProcessId())) return false;
    auto data = reinterpret_cast<COPYDATASTRUCT*>(payload);
    if (!data || data->dwData != Protocol || !data->lpData || data->cbData < 4 ||
        data->cbData > 65522 || data->cbData % sizeof(wchar_t)) return false;
    auto text = static_cast<const wchar_t*>(data->lpData);
    auto length = data->cbData / sizeof(wchar_t);
    if (text[length - 1] != L'\0') return false;
    std::wstring path(text, length - 1);
    if (!ValidPath(path)) return false;
    pending = path;
    if (!PostMessageW(window, InstallMessage, 0, 0)) { pending.reset(); return false; }
    return true;
}

// Return -1 to start the server; any other value is the launcher's exit code.
inline int PrepareLaunch() {
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 2;
    std::vector<std::wstring> arguments(args, args + count);
    LocalFree(args);
    if (count == 2 && arguments[1] == L"--forget-credentials") { ForgetCredentials(); return 0; }
    if (count != 1 && (count != 3 || arguments[1] != L"--install")) {
        Alert(L"使用方法: AltServer.exe --install \"IPAの絶対パス\"\n保存情報の削除: --forget-credentials"); return 2;
    }
    if (count == 3 && !ValidPath(arguments[2])) {
        Alert(L"IPAファイルを読み取れません。ローカルにある有効なIPAファイルを指定してください。"); return 2;
    }
    auto sid = UserSID(GetCurrentProcessId());
    if (sid.empty()) { Alert(L"Windowsユーザーを確認できませんでした。"); return 2; }
    endpoint = L"AltServerDirectIPA.v1." + sid;
    singletonMutex = CreateMutexW(nullptr, FALSE, (L"Local\\" + endpoint).c_str());
    if (!singletonMutex) { Alert(L"AltServerの起動状態を確認できませんでした。"); return 2; }
    auto acquired = WaitForSingleObject(singletonMutex, 0);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
        HWND existing = nullptr;
        for (int i = 0; i < 100 && !existing; ++i) {
            existing = FindWindowExW(HWND_MESSAGE, nullptr, endpoint.c_str(), endpoint.c_str());
            if (!existing) Sleep(50);
        }
        CloseHandle(singletonMutex);
        singletonMutex = nullptr;
        if (!existing) { Alert(L"AltServerの起動中です。少し待ってからもう一度実行してください。"); return 3; }
        if (count == 1) return 0;
        HWND sender = CreateWindowExW(0, L"STATIC", L"AltServerDirectIPA.Sender", 0,
            0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!sender) return 2;
        COPYDATASTRUCT data{Protocol, static_cast<DWORD>((arguments[2].size() + 1) * sizeof(wchar_t)), arguments[2].data()};
        DWORD_PTR accepted = 0;
        auto sent = SendMessageTimeoutW(existing, WM_COPYDATA, reinterpret_cast<WPARAM>(sender),
            reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &accepted);
        DestroyWindow(sender);
        if (!sent || accepted != 1) { Alert(L"AltServerが処理中か、要求を受け取れませんでした。処理の完了後にもう一度実行してください。"); return 3; }
        return 0;
    }
    if (FindWindowExW(HWND_MESSAGE, nullptr, L"DesktopApp", nullptr)) {
        Alert(L"既存のAltServerを終了してから改造版を起動してください。");
        ReleaseMutex(singletonMutex);
        CloseHandle(singletonMutex);
        singletonMutex = nullptr;
        return 3;
    }
    if (count == 3) pending = arguments[2];
    return -1;
}

inline void BeginInstall(HWND window) {
    if (busy || !pending) return;
    busy = true;
    State("discovering-devices");
    auto path = *pending;
    pending.reset();
    if (!ValidPath(path)) { Alert(L"IPAファイルが移動または削除されました。"); busy = false; return; }
    auto device = SelectDevice(window);
    if (!device) { busy = false; return; }
    _selectedDevice = device;
    _ipaFilepath = StringFromWideString(path);
    Credentials credential;
    if (!ReadCredentials(device->identifier(), credential)) {
        loginForDirectIPA = true;
        State("awaiting-apple-id");
        int result = DialogBoxW(hInst, MAKEINTRESOURCEW(ID_LOGIN), nullptr, LoginDlgProc);
        loginForDirectIPA = false;
        if (result != IDOK) {
            busy = false;
            State(result == -1 ? "login-dialog-failed" : "cancelled");
            if (result == -1) Alert(L"Apple IDの入力画面を開けませんでした。");
        }
        return;
    }
    // The task may ask for 2FA using AltServer's existing dialog. Do not block
    // the UI message loop while that dialog or signing is in progress.
    State("installing");
    auto task = AltServerApp::instance()->InstallApplication(_ipaFilepath, device,
        StringFromWideString(credential.user), StringFromWideString(credential.password));
    task.then([window](pplx::task<std::shared_ptr<Application>> result) {
        bool success = false;
        try { result.get(); success = true; } catch (...) { /* AltServer already presents the error. */ }
        PostMessageW(window, CompleteMessage, success ? 1 : 0, 0);
    });
}
}
