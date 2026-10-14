# Direct IPA Windows

FR-DIPA-1: IPA shell verb invokes modified AltServer directly; one available iPhone/iPad is automatic, multiple devices show a choice, zero devices report an actionable error.

FR-DIPA-2: Remember Apple credentials per device after successful installation, using Windows Credential Manager. Do not put credentials in arguments or files. Preserve existing Apple 2FA. Allow explicit removal of this feature's stored credentials.

DES-DIPA-1: A per-user message-only window accepts bounded WM_COPYDATA v1 messages carrying a local absolute IPA path. Validate sender Windows user, payload, extension and file type; reject concurrent work. A per-user named mutex prevents parallel instances. Reuse AltServerApp::InstallApplication for signing and transport.

DES-DIPA-2: The signing task runs asynchronously. The bridge and existing server protocol remain separate. Existing stock AltServer must be exited before this derivative starts; installed program files are not modified. Menus are per-user registry verbs with explicit uninstall. Japanese feature text is an explicit language fallback in this personal derivative.

TEST-DIPA-1: Native regression harness covers Unicode and space paths, invalid paths, credential round trip using synthetic credentials, invalid sender/protocol/payload, duplicate requests and busy state. Awaiting C++ build and test execution.

TEST-DIPA-2: Manual validation required: zero/one/two devices, initial credentials and 2FA, successful installation and subsequent saved-login installation, failures, password reset, switching back to stock AltServer. No actual device signing has been performed.

T-DIPA-1: Source changes, shell installer and Windows GitHub Actions prepared. Native compiler is absent locally. Building in a private GitHub repository requires user authorization to push, following root AGENTS.md. No push, release or installation has occurred.

Residual risk: This is based on AltForge 769fc63, not official AltServer 1.8 source. Apple API compatibility and real-device signing must be verified. CI requires hosted Windows runner capacity; untrusted IPA extraction inherits AltForge's archive validation. First-login persistence is after install success, not merely after submitting credentials.
