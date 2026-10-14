[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
$outputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$stagingDirectory = Join-Path $outputDirectory ("AltForge-AltServer-Windows-" + [Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $outputDirectory "AltForge-AltServer-Windows.zip"
$temporaryArchivePath = Join-Path $outputDirectory ("AltForge-AltServer-Windows-" + [Guid]::NewGuid().ToString("N") + ".zip")

function Copy-Dlls {
    param([Parameter(Mandatory = $true)][string]$Directory)

    if (Test-Path $Directory) {
        Get-ChildItem -LiteralPath $Directory -Filter "*.dll" -File | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $stagingDirectory -Force
        }
    }
}

New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $stagingDirectory | Out-Null

try {
    $releaseDirectory = Join-Path $root "Release"
    $win32ReleaseDirectory = Join-Path $root "Win32\Release"
    $vcpkgBinDirectory = Join-Path $root "vcpkg_installed\x86-windows\bin"
    $mdnsDirectory = Join-Path $root "Dependencies\mDNSResponder\mDNSWindows\DLL\Win32\Release"

    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "Visual Studio Installer could not be located."
    }
    $visualStudioRoot = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
    if (-not $visualStudioRoot) {
        throw "A Visual Studio installation could not be located."
    }
    $redistRoot = Join-Path $visualStudioRoot "VC\Redist\MSVC"
    $runtimeDirectory = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "x86\Microsoft.VC143.CRT" } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
    if (-not $runtimeDirectory) {
        throw "The x86 Visual C++ runtime could not be located under $redistRoot."
    }

    $executable = Join-Path $releaseDirectory "AltServer.exe"
    if (-not (Test-Path $executable)) {
        throw "Missing Windows build output: $executable"
    }

    Copy-Item -LiteralPath $executable -Destination $stagingDirectory
    foreach ($file in @('install-direct-menu.ps1', 'Install-Menu.cmd', 'Uninstall-Menu.cmd', 'Forget-Apple-ID.cmd', 'DirectIPA-README-ja.md')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $stagingDirectory
    }
    Copy-Dlls -Directory $releaseDirectory
    Copy-Dlls -Directory $win32ReleaseDirectory
    Copy-Dlls -Directory $vcpkgBinDirectory
    Copy-Dlls -Directory $mdnsDirectory
    Copy-Dlls -Directory $runtimeDirectory

    $requiredFiles = @(
        "AltServer.exe",
        "ldid.dll",
        "imobiledevice.dll",
        "usbmuxd.dll",
        "plist.dll",
        "dnssd.dll",
        "cpprest_2_10.dll",
        "msvcp140.dll",
        "vcruntime140.dll"
    )

    foreach ($requiredFile in $requiredFiles) {
        if (-not (Test-Path (Join-Path $stagingDirectory $requiredFile))) {
            throw "Missing required runtime file: $requiredFile"
        }
    }

    $requiredPatterns = @(
        "libcrypto-*.dll",
        "libssl-*.dll",
        "pcre2-8.dll",
        "pcre2-posix.dll",
        "z.dll"
    )
    foreach ($requiredPattern in $requiredPatterns) {
        if (-not (Get-ChildItem -LiteralPath $stagingDirectory -Filter $requiredPattern -File)) {
            throw "Missing required runtime file matching: $requiredPattern"
        }
    }

    $notice = @"
AltForge Server for Windows

Run AltServer.exe from this extracted directory. Apple website versions of
iTunes and iCloud are required. Source and license information:
https://github.com/legeling/AltForge
"@
    Set-Content -LiteralPath (Join-Path $stagingDirectory "README.txt") -Value $notice -Encoding UTF8

    Copy-Item -LiteralPath (Join-Path $root "..\LICENSE") -Destination (Join-Path $stagingDirectory "LICENSE.txt")
    $licensesDirectory = Join-Path $stagingDirectory "third-party-licenses"
    New-Item -ItemType Directory -Path $licensesDirectory | Out-Null

    $sourceLicenses = @{
        "apple-mDNSResponder.txt" = (Join-Path $root "Dependencies\mDNSResponder\LICENSE")
        "dirent.txt" = (Join-Path $root "Dependencies\dirent\LICENSE")
        "js-srp-gsa-ISC.txt" = (Join-Path $root "AltSign\Dependencies\js-srp-gsa-LICENSE.txt")
        "minizip-zlib.txt" = (Join-Path $root "AltSign\Dependencies\minizip-LICENSE.txt")
        "mman-win32-MIT.txt" = (Join-Path $root "AltSign\Dependencies\mman-LICENSE.txt")
        "ldid-AGPL-3.0.txt" = (Join-Path $root "ldid\COPYING")
        "libimobiledevice-vs.txt" = (Join-Path $root "Dependencies\libimobiledevice-vs\LICENSE")
        "libimobiledevice-GPL-2.0.txt" = (Join-Path $root "Dependencies\libimobiledevice-vs\libimobiledevice\COPYING")
        "libimobiledevice-LGPL-2.1.txt" = (Join-Path $root "Dependencies\libimobiledevice-vs\libimobiledevice\COPYING.LESSER")
        "libplist-GPL-2.0.txt" = (Join-Path $root "Dependencies\libimobiledevice-vs\libplist\COPYING")
        "libplist-LGPL-2.1.txt" = (Join-Path $root "Dependencies\libimobiledevice-vs\libplist\COPYING.LESSER")
        "libusbmuxd-LGPL-2.1.txt" = (Join-Path $root "Dependencies\libimobiledevice-vs\libusbmuxd\COPYING")
    }
    foreach ($entry in $sourceLicenses.GetEnumerator()) {
        if (-not (Test-Path $entry.Value)) {
            throw "Missing dependency license: $($entry.Value)"
        }
        Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $licensesDirectory $entry.Key)
    }

    $vcpkgShareDirectory = Join-Path $root "vcpkg_installed\x86-windows\share"
    Get-ChildItem -LiteralPath $vcpkgShareDirectory -Filter "copyright" -File -Recurse | ForEach-Object {
        $packageName = Split-Path -Leaf (Split-Path -Parent $_.FullName)
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licensesDirectory "$packageName.txt") -Force
    }

    Compress-Archive -Path (Join-Path $stagingDirectory "*") -DestinationPath $temporaryArchivePath -CompressionLevel Optimal
    if (-not (Test-Path $temporaryArchivePath) -or (Get-Item -LiteralPath $temporaryArchivePath).Length -eq 0) {
        throw "Windows release archive is empty."
    }
    if (Test-Path $archivePath) {
        [System.IO.File]::Replace($temporaryArchivePath, $archivePath, $null)
    }
    else {
        [System.IO.File]::Move($temporaryArchivePath, $archivePath)
    }
    Write-Host "Created $archivePath"
}
finally {
    if (Test-Path $temporaryArchivePath) {
        Remove-Item -LiteralPath $temporaryArchivePath -Force
    }
    if (Test-Path $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
}
