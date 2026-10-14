# Upstream provenance

This directory is derived from [rileytestut/AltServer-Windows](https://github.com/rileytestut/AltServer-Windows).

- Upstream branch: `1.7.4`
- Upstream commit: `f4dba95b66e040540fbf16e2b460dc1517c4d864`
- Imported: 2026-08-09
- Upstream license: GNU Affero General Public License v3.0

The OpenSSL SRP-6a adaptation is based on `icloudjs/js-srp-gsa` commit `0e5e5452e79b8ebc71f12e4eda64f29a22ef5dbc` under its ISC license.

The import contains the Visual Studio build graph used by the Windows product. Historical Xcode experiments, legacy installer/AppX projects, duplicate standalone libplist/libusbmuxd trees, unused ldid copies, generated resource output, prebuilt Visual C++ runtimes, and debug symbol files were intentionally omitted. The original gitlink dependencies and the three libimobiledevice source trees required by its Visual Studio projects are restored at pinned commits by `Scripts/bootstrap-dependencies.ps1` rather than represented as nested repositories in AltForge.

AltForge-specific changes are kept in this directory and include product/source identity, reproducible dependency paths, modern Visual Studio toolset support, packaging, and release integration. The upstream redistribution-incompatible Apple corecrypto binary and headers are not imported; Windows SRP-6a uses OpenSSL and the attributed ISC-licensed algorithm adaptation instead. The repository root `LICENSE` applies to AltForge modifications; bundled third-party components retain their own license files.
