import Foundation
let token = String(repeating:"a",count:64)
for address in ["http://10.77.0.1:49321", "http://100.64.0.1:49321", "http://192.168.7.1:49321"] {
    guard (try? remoteEndpoint(address:address,token:token,path:"/api/apps")) != nil else { fatalError("Valid VPN address rejected") }
}
for address in ["https://10.77.0.1:49321", "http://8.8.8.8:49321", "http://100.128.0.1:49321",
                "http://10.77.0.1.attacker.invalid:49321", "http://10.077.0.1:49321", "http://10.77..1:49321",
                "http://user@10.77.0.1:49321", "http://10.77.0.1:49321/?token=secret", "http://10.77.0.1:80"] {
    guard (try? remoteEndpoint(address:address,token:token,path:"/api/apps")) == nil else { fatalError("Unsafe endpoint accepted") }
}
guard (try? remoteEndpoint(address:"http://10.77.0.1:49321",token:"wrong",path:"/api/apps")) == nil else { fatalError("Invalid token accepted") }
print("Endpoint validation passed (VPN addresses, hostile hostnames, credentials, query and token cases).")
