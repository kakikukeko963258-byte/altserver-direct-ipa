[CmdletBinding()]
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$menu = 'HKCU:\Software\Classes\SystemFileAssociations\.ipa\shell\AltServerDirectIPA'
if ($Uninstall) {
    if (Test-Path -LiteralPath $menu) { Remove-Item -LiteralPath $menu -Recurse -Force }
    Write-Host '右クリックメニューを削除しました。保存したApple IDは削除していません。'
    exit
}
$executable = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'AltServer.exe'))
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'ビルド済みAltServer.exeと同じフォルダーで実行してください。'
}
# Keep the existing IPA default application; register a per-user shell verb.
New-Item -Path "$menu\command" -Force | Out-Null
Set-Item -LiteralPath $menu -Value 'AltServerでiPhoneにインストール'
New-ItemProperty -LiteralPath $menu -Name Icon -Value $executable -PropertyType String -Force | Out-Null
New-ItemProperty -LiteralPath $menu -Name MultiSelectModel -Value 'Single' -PropertyType String -Force | Out-Null
Set-Item -LiteralPath "$menu\command" -Value ('"' + $executable + '" --install "%1"')
Write-Host 'IPAの右クリックメニューを登録しました。Windows 11では「その他のオプションを確認」に表示されます。'
