# Remote installation (active)

FR: With the Windows PC running at home, an iOS 26.6.1 phone on cellular can request installation of an approved IPA. Existing USB/LAN signing and certificate reuse must continue working.

DES: A foreground SwiftUI controller talks only to a configured private VPN address. A bearer token authorizes listing a local allowlist and starting one job. Windows signs through the existing AltServer pipeline; a pinned `idevice` worker authenticates the registered phone through RPPairing, creates a TLS-PSK CDTunnel, and invokes AFC/InstallationProxy over RSD. Verify the phone UDID before signing and again before upload. Never publish pairing records, tokens, IPA inputs or account data. No public listener, arbitrary file upload, shell endpoint or certificate export. Bind only the explicitly configured IPv4 VPN address. VPN setup and USB trust remain explicit user steps.

TEST: Build Windows native code and worker on Windows Actions; protocol/auth/path/replay/job tests with synthetic data; build the unsigned controller IPA with Xcode on a macOS runner. Physical acceptance requires USB pairing, a reachable VPN peer, cellular-only phone, install success confirmed by InstallationProxy and app launch. These checks must be reported separately from CI.

T: Implement worker/setup, native dispatch, iOS controller, workflows and instructions. Do not replace the installed stable release until build checks pass. Cellular operation is experimental until physically verified.
