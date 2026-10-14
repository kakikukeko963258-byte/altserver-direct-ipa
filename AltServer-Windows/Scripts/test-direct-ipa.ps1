$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
if (-not $vs) { throw 'Visual Studio C++ toolchain is required.' }
$development = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$testDirectory = Join-Path $root 'Tests'
$includeDirectory = Join-Path $root 'vcpkg_installed\x86-windows\include'
$libraryDirectory = Join-Path $root 'vcpkg_installed\x86-windows\lib'
$fixture = Join-Path $testDirectory 'DirectIPATests.cpp'
$executable = Join-Path $testDirectory 'DirectIPATests.exe'
$objectFile = Join-Path $testDirectory 'DirectIPATests.obj'
$plistInclude = Join-Path $root 'Dependencies\libimobiledevice-vs\libplist\include'
$releaseLibraries = Join-Path $root 'Release'
# Fixed paths are quoted as cmd tokens; account data is never used here.
$compile = 'call "' + $development + '" && cl /nologo /EHsc /std:c++17 /utf-8 /MD /I"' + $includeDirectory + '" /I"' + $plistInclude + '" "' + $fixture + '" /Fe:"' + $executable + '" /Fo:"' + $objectFile + '" /link /LIBPATH:"' + $libraryDirectory + '" /LIBPATH:"' + $releaseLibraries + '" cpprest_2_10.lib libcurl.lib libcrypto.lib plist.lib ole32.lib user32.lib'
& cmd.exe /d /s /c $compile
if ($LASTEXITCODE -ne 0) { throw 'Direct IPA test compilation failed.' }
$previousPath = $env:PATH
try {
    $env:PATH = (Join-Path $root 'vcpkg_installed\x86-windows\bin') + ';' + $releaseLibraries + ';' + $env:PATH
    & $executable
    if ($LASTEXITCODE -ne 0) { throw 'Direct IPA bridge tests failed.' }
} finally { $env:PATH = $previousPath }
