<p align="center">
  <img src="../docs/assets/brand/altforge-wordmark.png" width="420" alt="AltForge">
</p>

# AltForge Server for Windows

This directory contains the Windows desktop service used to install and refresh AltForge on an iPhone or iPad. It is maintained in the same repository as the iOS client and macOS service; see `UPSTREAM.md` for the imported upstream revision.

## User requirements

- Windows 10 or later, with 64-bit Windows supported through the Win32 application compatibility layer.
- iTunes and iCloud downloaded from Apple's website, not the Microsoft Store. These install Apple Mobile Device Support, Bonjour, and the anisette components used by AltForge Server.
- A trusted USB connection for initial setup. Wi-Fi refresh requires the device and computer to be on the same network.

The GitHub Release ZIP is portable and unsigned. Extract the complete directory, run `AltServer.exe`, then choose **Install AltForge** from its notification-area menu. Do not run only the executable without the adjacent DLLs.

The imported Windows notification-area interface currently retains its upstream English text. Simplified Chinese localization and language switching apply to the iOS client; Windows desktop localization is scoped separately rather than being implied by this source integration.

## Build requirements

- Windows 10 or later.
- Visual Studio 2022 with Desktop development with C++, MSVC v143, a Windows 10/11 SDK, and Clang tools for Windows.
- Git, PowerShell 7 or Windows PowerShell 5.1, and vcpkg.
- `VCPKG_ROOT` or `VCPKG_INSTALLATION_ROOT` pointing to vcpkg.

Build and package a release archive from the repository root:

```powershell
.\AltServer-Windows\Scripts\build-release.ps1 -OutputDirectory "$env:TEMP\AltForge-Windows"
```

The root `VERSION` file is the shared AltForge product version. The script rejects an explicit `-Version` that differs from it, restores six pinned source repositories, installs the manifest dependencies for `x86-windows`, builds Apple's open-source DNS-SD client library, builds the `AltServer` solution target, checks the runtime DLL and license contract, and writes `AltForge-AltServer-Windows.zip`.

AltForge currently publishes a ZIP rather than importing the legacy `.vdproj`/AppX packaging projects. Those toolchains are not reliably available on hosted runners, and their output would still be unsigned.

## Dependency policy

`Scripts/bootstrap-dependencies.ps1` fetches only fixed commits and rejects an existing checkout at a different revision. `vcpkg.json` pins its registry baseline. GitHub Actions additionally checks out that exact vcpkg revision into the workspace instead of relying on the runner's mutable system checkout. Generated dependency directories and build output are ignored and are never committed.

The release does not redistribute iTunes, iCloud, Apple Mobile Device Support, or Apple account data. Keep Apple IDs, passwords, verification codes, device identifiers, certificates, and anisette data out of logs and build artifacts.
