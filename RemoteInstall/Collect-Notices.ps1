param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$metadata = (& cargo metadata --locked --format-version 1 | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0) { throw 'Cargo metadata failed.' }
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
foreach ($package in $metadata.packages) {
    $source = Split-Path -Parent $package.manifest_path
    $target = Join-Path $OutputDirectory (($package.name + '-' + $package.version) -replace '[^A-Za-z0-9._+-]','_')
    [IO.Directory]::CreateDirectory($target) | Out-Null
    $summary = [ordered]@{name=$package.name;version=$package.version;license=$package.license;repository=$package.repository;source=$package.source}
    [IO.File]::WriteAllText((Join-Path $target 'PACKAGE.json'),($summary | ConvertTo-Json))
    $files = @(Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Name -match '^(UN)?LICEN[CS]E|^COPYING|^NOTICE|^COPYRIGHT' })
    if ($package.license_file) {
        $candidate = if ([IO.Path]::IsPathRooted($package.license_file)) { $package.license_file } else { Join-Path $source $package.license_file }
        $candidate = [IO.Path]::GetFullPath($candidate)
        $prefix = [IO.Path]::GetFullPath($source).TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
        if ($candidate.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            $files += Get-Item -LiteralPath $candidate
        }
    }
    foreach ($file in ($files | Sort-Object FullName -Unique)) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $target $file.Name)
        }
    }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../LICENSE') -Destination (Join-Path $OutputDirectory 'AltForge-AGPL-3.0.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Notices/idevice-LICENSE.txt') -Destination (Join-Path $OutputDirectory 'idevice-LICENSE.txt')
$compiler = (& rustc --version) -join ' '
[IO.File]::WriteAllText((Join-Path $OutputDirectory '../BUILD-INFO.txt'),"Source=$env:GITHUB_SHA`nCompiler=$compiler`nNetwork=Acquire on Wi-Fi, retain over cellular; physical roaming validation pending.`n")
Write-Host "Collected notices for $($metadata.packages.Count) dependency packages."
