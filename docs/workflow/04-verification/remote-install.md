# Remote installation verification

Implementation is a preview. Basic cellular installation was physically verified on the intended phone on 2026-10-16. Recovery/failure acceptance remains pending.

## Automated evidence

- Native Windows build + existing bridge/auth/CSR/compression/menu tests + actual ZIP extraction (directories, UTF-8, traversal, device aliases, symlinks, CRC): Actions run 38821971250, source c5e1f23, passed.
- Persistent broker + API/DPAPI + mutually authenticated IPC + actual Windows named-pipe roundtrip: run 38846063098, source 67191d7, passed. The subsequent healthy-tunnel retention change requires the final worker run recorded in the delivered BUILD-STATUS file.
- Swift endpoint/QR validation and Xcode unsigned device IPA: run 38846063098, source 67191d7, passed.
- PowerShell setup/start/test scripts: parser checks passed locally. No local account or pairing data was passed to CI.

## Physical evidence and gaps

- Tailscale on Windows and the intended iPhone: both online, ping succeeded.
- Cellular-only fresh TCP probes: ports 49152 and 62078 not reachable. This is consistent with the acquisition boundary; it is not a successful device-service connection.
- A native global state file reported success during an earlier controller installation attempt. Its target was not correlated to the outdoor iPhone, so it does not establish that the controller was installed on that phone.
- Home test 2026-10-16: exactly one intended USB phone was registered. The first VPN identity probe succeeded; repeated IPC probes exposed a Windows pipe reuse failure. The setup ACL failure and IPC lifetime handling were repaired.
- Worker da3159c, Actions 38882542949: all 11 tests passed, including 20 consecutive real Windows named-pipe sessions. Deployed to the separate preview folder; three consecutive physical identity probes passed.
- Remote native installation of the approved test IPA over the VPN completed while on Wi-Fi. The user then reported disconnecting USB, turning Wi-Fi off, requesting installation from the iPhone controller, and opening the installed app.
- The controller API job and the dedicated native state both reported succeeded at 18:48 JST. A post-install identity probe through the existing tunnel succeeded; a fresh TCP probe to the phone VPN port 49152 failed. This supports retained-tunnel operation after roaming rather than fresh cellular acquisition. Windows PnP still showed an Apple phone entry; it was not used as proof that all USB phones were absent.
- No general compatibility claim: existing-trusted-Wi-Fi registration, intentional session-loss/reacquisition, and policy/app-slot rejection recovery remain unverified.

## Acceptance when the phone is available

1. Connect exactly the intended unlocked phone over USB, or explicitly try its existing trusted local Wi-Fi connection. Register it locally and keep all pairing records private.
2. With Wi-Fi and Tailscale active, start the preview broker. Test-Remote must succeed and the controller must show the device connection as active.
3. Switch the phone to cellular while leaving Tailscale, Windows and the broker running. Confirm a fresh device-port connection is closed while the existing bridge still works.
4. Request an approved test IPA from the phone, verify InstallationProxy completion and open the app. Repeat another request without reacquiring the outer tunnel.
5. Try an install rejected by device policy/app-slot limits: return failure and retain the tunnel if device identity verification still works.
6. Intentionally lose the bridge. The controller must disable installation, and a request must be rejected before Apple authentication/signing. Join any Wi-Fi and verify reacquisition, then repeat the cellular test.

Keep production/stable AltServer untouched until the intended device is identified. Never silently substitute a different phone that happens to be reachable at the PC.
