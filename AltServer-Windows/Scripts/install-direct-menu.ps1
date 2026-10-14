[CmdletBinding()]
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$menu = 'HKCU:\Software\Classes\SystemFileAssociations\.ipa\shell\AltServerDirectIPA'
$settings = 'HKCU:\Software\AltServerDirectIPA'
$serverSettings = 'HKCU:\Software\AltForge\AltServer'
$run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$executable = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'AltServer.exe'))
function Ensure-RegistryKey([string]$key) {
    # Registry New-Item -Force can replace existing keys. Never use it on
    # existing preference or Windows startup keys.
    if (-not (Test-Path -LiteralPath $key)) { New-Item -Path $key | Out-Null }
}
function Read-RegistryValue([string]$key, [string]$name) {
    if (-not (Test-Path -LiteralPath $key)) { return $null }
    $item = Get-ItemProperty -LiteralPath $key
    $property = $item.PSObject.Properties[$name]
    if ($property) { return $property.Value }
    return $null
}
if ($Uninstall) {
    if (Test-Path -LiteralPath $menu) { Remove-Item -LiteralPath $menu -Recurse -Force }
    $modifiedStartup = Read-RegistryValue $run 'AltForge Server'
    if ($modifiedStartup -and $modifiedStartup.Trim('"') -eq $executable) {
        Remove-ItemProperty -LiteralPath $run -Name 'AltForge Server'
        if (Test-Path -LiteralPath $serverSettings) {
            New-ItemProperty -LiteralPath $serverSettings -Name LaunchAtStartup -Value 0 -PropertyType DWord -Force | Out-Null
        }
    }
    $backup = Get-ItemProperty -LiteralPath $settings -ErrorAction SilentlyContinue
    $currentStock = Read-RegistryValue $run 'AltServer'
    if ($backup -and $backup.StockStartupWasPresent -eq 1 -and -not $currentStock) {
        New-ItemProperty -LiteralPath $run -Name AltServer -Value $backup.StockStartupCommand -PropertyType String -Force | Out-Null
    }
    Write-Host '右クリックメニューを削除しました。保存したApple IDは削除していません。'
    exit
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'ビルド済みAltServer.exeと同じフォルダーで実行してください。'
}
Ensure-RegistryKey $serverSettings
$configuredFolder = Read-RegistryValue $serverSettings 'AppleFolder'
$stockFolder = Read-RegistryValue 'HKCU:\SOFTWARE\RileyTestut\AltServer' 'AppleFolder'
if (-not $configuredFolder -and $stockFolder) {
    $complete = $true
    foreach ($component in @('Apple Application Support\Foundation.dll', 'Apple Application Support\objc.dll', 'Internet Services\AOSKit.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $stockFolder $component) -PathType Leaf)) { $complete = $false }
    }
    if ($complete) {
        New-ItemProperty -LiteralPath $serverSettings -Name AppleFolder -Value $stockFolder -PropertyType String -Force | Out-Null
    }
}
# Replace the stock startup entry, retaining its command for uninstall.
# This prevents two AltServer implementations from starting after a reboot.
Ensure-RegistryKey $settings
Ensure-RegistryKey $run
$stockStartup = Read-RegistryValue $run 'AltServer'
$hasBackup = Read-RegistryValue $settings 'StockStartupWasPresent'
if ($stockStartup) {
    New-ItemProperty -LiteralPath $settings -Name StockStartupWasPresent -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -LiteralPath $settings -Name StockStartupCommand -Value $stockStartup -PropertyType String -Force | Out-Null
} elseif ($null -eq $hasBackup) {
    New-ItemProperty -LiteralPath $settings -Name StockStartupWasPresent -Value 0 -PropertyType DWord -Force | Out-Null
}
if ($stockStartup) { Remove-ItemProperty -LiteralPath $run -Name AltServer }
New-ItemProperty -LiteralPath $run -Name 'AltForge Server' -Value ('"' + $executable + '"') -PropertyType String -Force | Out-Null
New-ItemProperty -LiteralPath $serverSettings -Name LaunchAtStartup -Value 1 -PropertyType DWord -Force | Out-Null
# Keep the existing IPA default application; register a per-user shell verb.
Ensure-RegistryKey "$menu\command"
Set-Item -LiteralPath $menu -Value 'AltServerでiPhoneにインストール'
New-ItemProperty -LiteralPath $menu -Name Icon -Value $executable -PropertyType String -Force | Out-Null
New-ItemProperty -LiteralPath $menu -Name MultiSelectModel -Value 'Single' -PropertyType String -Force | Out-Null
Set-Item -LiteralPath "$menu\command" -Value ('"' + $executable + '" --install "%1"')
Write-Host 'IPAの右クリックメニューを登録しました。Windows 11では「その他のオプションを確認」に表示されます。'
