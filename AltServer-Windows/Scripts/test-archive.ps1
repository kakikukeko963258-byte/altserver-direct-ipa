$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
if (-not $vs) { throw 'Visual Studio C++ toolchain required.' }
$development = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$test = Join-Path $root 'Tests\ArchiveTests.cpp'
$exe = Join-Path $root 'Tests\ArchiveTests.exe'
$obj = Join-Path $root 'Tests\ArchiveTests.obj'
$include = Join-Path $root 'AltSign\Dependencies\minizip'
$library = Join-Path $root 'vcpkg_installed\x86-windows\lib'
$release = Join-Path $root 'Release'
$win32 = Join-Path $root 'Win32\Release'
$compile = 'call "' + $development + '" && cl /nologo /EHsc /std:c++17 /utf-8 /MD /I"' + $include + '" "' + $test + '" /Fe:"' + $exe + '" /Fo:"' + $obj + '" /link /LIBPATH:"' + $library + '" /LIBPATH:"' + $release + '" /LIBPATH:"' + $win32 + '" AltSign.lib cpprest_2_10.lib z.lib'
& cmd.exe /d /s /c $compile
if ($LASTEXITCODE -ne 0) { throw 'Archive test compilation failed.' }
$savedPath = $env:PATH
try {
    $env:PATH = (Join-Path $root 'vcpkg_installed\x86-windows\bin') + ';' + $release + ';' + $win32 + ';' + $env:PATH
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Archive regression failed.' }
} finally { $env:PATH = $savedPath }
