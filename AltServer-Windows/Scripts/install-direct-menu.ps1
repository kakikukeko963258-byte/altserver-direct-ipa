[CmdletBinding()]
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$menu = 'HKCU:\Software\Classes\SystemFileAssociations\.ipa\shell\AltServerDirectIPA'
$settings = 'HKCU:\Software\AltServerDirectIPA'
$serverSettings = 'HKCU:\Software\AltForge\AltServer'
$run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$executable = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'AltServer.exe'))
if ($Uninstall) {
    if (Test-Path -LiteralPath $menu) { Remove-Item -LiteralPath $menu -Recurse -Force }
    $modifiedStartup = Get-ItemPropertyValue -LiteralPath $run -Name 'AltForge Server' -ErrorAction SilentlyContinue
    if ($modifiedStartup -and $modifiedStartup.Trim('"') -eq $executable) {
        Remove-ItemProperty -LiteralPath $run -Name 'AltForge Server'
        if (Test-Path -LiteralPath $serverSettings) {
            New-ItemProperty -LiteralPath $serverSettings -Name LaunchAtStartup -Value 0 -PropertyType DWord -Force | Out-Null
        }
    }
    $backup = Get-ItemProperty -LiteralPath $settings -ErrorAction SilentlyContinue
    $currentStock = Get-ItemPropertyValue -LiteralPath $run -Name AltServer -ErrorAction SilentlyContinue
    if ($backup -and $backup.StockStartupWasPresent -eq 1 -and -not $currentStock) {
        New-ItemProperty -LiteralPath $run -Name AltServer -Value $backup.StockStartupCommand -PropertyType String -Force | Out-Null
    }
    Write-Host '右クリックメニューを削除しました。保存したApple IDは削除していません。'
    exit
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'ビルド済みAltServer.exeと同じフォルダーで実行してください。'
}
New-Item -Path $serverSettings -Force | Out-Null
$configuredFolder = Get-ItemPropertyValue -LiteralPath $serverSettings -Name AppleFolder -ErrorAction SilentlyContinue
$stockFolder = Get-ItemPropertyValue -LiteralPath 'HKCU:\SOFTWARE\RileyTestut\AltServer' -Name AppleFolder -ErrorAction SilentlyContinue
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
New-Item -Path $settings -Force | Out-Null
New-Item -Path $run -Force | Out-Null
$stockStartup = Get-ItemPropertyValue -LiteralPath $run -Name AltServer -ErrorAction SilentlyContinue
$hasBackup = Get-ItemPropertyValue -LiteralPath $settings -Name StockStartupWasPresent -ErrorAction SilentlyContinue
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
New-Item -Path "$menu\command" -Force | Out-Null
Set-Item -LiteralPath $menu -Value 'AltServerでiPhoneにインストール'
New-ItemProperty -LiteralPath $menu -Name Icon -Value $executable -PropertyType String -Force | Out-Null
New-ItemProperty -LiteralPath $menu -Name MultiSelectModel -Value 'Single' -PropertyType String -Force | Out-Null
Set-Item -LiteralPath "$menu\command" -Value ('"' + $executable + '" --install "%1"')
Write-Host 'IPAの右クリックメニューを登録しました。Windows 11では「その他のオプションを確認」に表示されます。'
