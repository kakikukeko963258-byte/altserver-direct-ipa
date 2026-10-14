$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Registry integration tests run only on the disposable GitHub runner.' }
$menu = 'HKCU:\Software\Classes\SystemFileAssociations\.ipa\shell\AltServerDirectIPA'
$settings = 'HKCU:\Software\AltServerDirectIPA'
$serverSettings = 'HKCU:\Software\AltForge\AltServer'
$stockSettings = 'HKCU:\Software\RileyTestut\AltServer'
$run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$sentinel = 'AltServerDirectIPA-Test-' + [guid]::NewGuid().ToString('N')
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixture = [IO.Path]::GetFullPath((Join-Path $temporaryRoot ('Direct IPA Test ' + [guid]::NewGuid().ToString('N'))))
if (-not $fixture.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture is outside the temporary directory.' }
function Read-Value([string]$key, [string]$name) {
    if (-not (Test-Path -LiteralPath $key)) { return $null }
    $property = (Get-ItemProperty -LiteralPath $key).PSObject.Properties[$name]
    if ($property) { return $property.Value }
    return $null
}
function Check([bool]$condition, [string]$label) {
    if (-not $condition) { throw ('Registry test failed: ' + $label) }
}
foreach ($key in @($menu, $settings, $serverSettings, $stockSettings)) {
    if (Test-Path -LiteralPath $key) { throw ('Refusing to overwrite pre-existing fixture key: ' + $key) }
}
if ((Read-Value $run 'AltServer') -or (Read-Value $run 'AltForge Server')) { throw 'An AltServer startup entry already exists; refusing to run integration tests.' }
$stockCommand = '"C:\Synthetic Test\AltServer.exe"'
try {
    New-Item -ItemType Directory -Path $fixture | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install-direct-menu.ps1') -Destination $fixture
    Set-Content -LiteralPath (Join-Path $fixture 'AltServer.exe') -Value 'synthetic placeholder; never executed'
    $appleFolder = Join-Path $fixture 'Apple'
    foreach ($component in @('Apple Application Support\Foundation.dll', 'Apple Application Support\objc.dll', 'Internet Services\AOSKit.dll')) {
        $file = Join-Path $appleFolder $component
        New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force | Out-Null
        Set-Content -LiteralPath $file -Value 'synthetic placeholder; never loaded'
    }
    New-Item -Path $stockSettings | Out-Null
    New-ItemProperty -LiteralPath $stockSettings -Name AppleFolder -Value $appleFolder -PropertyType String | Out-Null
    if (-not (Test-Path -LiteralPath $run)) { New-Item -Path $run | Out-Null }
    New-ItemProperty -LiteralPath $run -Name $sentinel -Value 'preserve existing startup value' -PropertyType String | Out-Null
    New-ItemProperty -LiteralPath $run -Name AltServer -Value $stockCommand -PropertyType String | Out-Null
    $script = Join-Path $fixture 'install-direct-menu.ps1'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script
    Check ($LASTEXITCODE -eq 0) 'install returns success'
    Check ((Read-Value $run $sentinel) -eq 'preserve existing startup value') 'unrelated startup values survive'
    Check ((Read-Value $serverSettings AppleFolder) -eq $appleFolder) 'migrate portable Apple folder'
    Check ((Read-Value $settings StockStartupCommand) -eq $stockCommand) 'backup stock startup'
    Check ($null -eq (Read-Value $run AltServer)) 'disable duplicate stock startup'
    $executable = Join-Path $fixture 'AltServer.exe'
    Check ((Read-Value "$menu\command" '(default)') -eq ('"' + $executable + '" --install "%1"')) 'quoted shell command'
    Check ((Read-Value $run 'AltForge Server') -eq ('"' + $executable + '"')) 'quoted modified startup'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script
    Check ($LASTEXITCODE -eq 0) 'repeat install succeeds'
    Check ((Read-Value $settings StockStartupCommand) -eq $stockCommand) 'repeat install preserves backup'
    Check ((Read-Value $serverSettings AppleFolder) -eq $appleFolder) 'repeat install preserves preferences'
    Check ((Read-Value $run $sentinel) -eq 'preserve existing startup value') 'repeat install preserves unrelated startup'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -Uninstall
    Check ($LASTEXITCODE -eq 0) 'uninstall succeeds'
    Check (-not (Test-Path -LiteralPath $menu)) 'shell verb removed'
    Check ((Read-Value $run AltServer) -eq $stockCommand) 'stock startup restored'
    Check ($null -eq (Read-Value $run 'AltForge Server')) 'modified startup removed'
    Check ((Read-Value $run $sentinel) -eq 'preserve existing startup value') 'uninstall preserves unrelated startup'
    Write-Host 'Direct IPA registry integration checks passed.'
} finally {
    foreach ($key in @($menu, $settings, $serverSettings, $stockSettings)) {
        if (Test-Path -LiteralPath $key) { Remove-Item -LiteralPath $key -Recurse -Force }
    }
    foreach ($value in @($sentinel, 'AltServer', 'AltForge Server')) {
        if ($null -ne (Read-Value $run $value)) { Remove-ItemProperty -LiteralPath $run -Name $value }
    }
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
