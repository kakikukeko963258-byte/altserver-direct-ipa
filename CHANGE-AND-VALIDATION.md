# Direct IPA Windows

FR-DIPA-1: IPA shell verb invokes modified AltServer directly; one available iPhone/iPad is automatic, multiple devices show a choice, zero devices report an actionable error.

FR-DIPA-2: Remember Apple credentials per device after successful installation, using Windows Credential Manager. Do not put credentials in arguments or files. Preserve existing Apple 2FA. Allow explicit removal of this feature's stored credentials.

DES-DIPA-1: A per-user message-only window accepts bounded WM_COPYDATA v1 messages carrying a local absolute IPA path. Validate sender Windows user, payload, extension and file type; reject concurrent work. A per-user named mutex prevents parallel instances. Reuse AltServerApp::InstallApplication for signing and transport.

DES-DIPA-2: The signing task runs asynchronously. The bridge and existing server protocol remain separate. Existing stock AltServer must be exited before this derivative starts; installed program files are not modified. Menus are per-user registry verbs with explicit uninstall. Japanese feature text is an explicit language fallback in this personal derivative.

TEST-DIPA-1: Native regression harness covers zero/one/multiple devices, USB/Wi-Fi deduplication, Unicode and space paths, invalid paths, credential round trip using synthetic credentials, invalid sender/protocol/payload, duplicate requests and busy state. Windows hosted build and all native checks passed in run 38579202220 at source commit 28838d1.

TEST-DIPA-2: USB discovery and lockdown communication succeeded with one attached physical device. The standalone initial-login dialog is visible and was brought to the foreground. Apple account authentication, successful installation and a subsequent saved-login installation are awaiting the user's initial login. Two physical devices, 2FA, failure recovery and password reset remain manual validation items.

TEST-DIPA-3: Disposable Windows runner registry integration checks passed in run 38579202220. They verify setup, repeated setup, Apple folder/server ID migration, preservation of unrelated startup entries, quoted commands, uninstall and restoration of stock startup.

T-DIPA-1: User authorized public builds. Source is published at https://github.com/kakikukeko963258-byte/altserver-direct-ipa. The compiled runtime is installed as a portable directory and the per-user IPA verb is registered on the user's PC. Apple dependencies, original server identity and the encrypted signing cache were reused only locally. User account data, device identifiers, keys and IPA inputs are excluded from the repository and build artifacts.

T-DIPA-2: Runtime startup and dialogs required fixes during physical smoke testing: use the configured Apple folder in both loaders, display login/device dialogs independently of the message-only server HWND, and create registry keys with CreateSubKey to preserve existing values. Native tests and registry regression tests cover the corresponding bridge/setup changes; the actual signing transaction still requires the user login.

Residual risk: This is based on AltForge 769fc63, not official AltServer 1.8 source. Apple API compatibility and real-device signing must be verified. CI requires hosted Windows runner capacity; untrusted IPA extraction inherits AltForge's archive validation. First-login persistence is after install success, not merely after submitting credentials.
