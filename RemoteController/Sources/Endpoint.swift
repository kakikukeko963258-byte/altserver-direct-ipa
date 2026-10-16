import Foundation

enum EndpointError: Error { case invalid }
func remoteEndpoint(address: String, token: String, path: String) throws -> URL {
    guard let components = URLComponents(string: address), components.scheme == "http",
          let host = components.host, components.port == 49321,
          components.user == nil, components.password == nil,
          components.query == nil, components.fragment == nil,
          components.path.isEmpty || components.path == "/" else { throw EndpointError.invalid }
    let parts = host.split(separator: ".", omittingEmptySubsequences: false)
    let bytes = parts.compactMap { UInt8($0) }
    guard parts.count == 4, bytes.count == 4,
          bytes.map({String($0)}).joined(separator: ".") == host,
          bytes[0] == 10 || (bytes[0] == 172 && (16...31).contains(bytes[1])) ||
            (bytes[0] == 192 && bytes[1] == 168) || (bytes[0] == 100 && (64...127).contains(bytes[1])),
          token.count == 64, token.allSatisfy({ $0.isHexDigit && $0.isASCII }),
          ["/api/apps","/api/status","/api/install","/api/connection"].contains(path),
          let url = URL(string: "http://\(host):49321\(path)") else { throw EndpointError.invalid }
    return url
}
func remoteConnection(from url: URL) -> (address: String, token: String)? {
    guard let parts = URLComponents(url:url,resolvingAgainstBaseURL:false),
          parts.scheme == "altforge-remote", parts.host == "setup", parts.path.isEmpty,
          parts.user == nil, parts.password == nil, parts.port == nil, parts.fragment == nil,
          let items = parts.queryItems, items.count == 2,
          items.filter({$0.name == "ip"}).count == 1,
          items.filter({$0.name == "token"}).count == 1,
          let ip = items.first(where:{$0.name == "ip"})?.value,
          let token = items.first(where:{$0.name == "token"})?.value else { return nil }
    let address = "http://\(ip):49321"
    guard (try? remoteEndpoint(address:address,token:token,path:"/api/apps")) != nil else { return nil }
    return (address,token)
}
