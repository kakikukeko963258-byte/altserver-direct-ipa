param([string]$AppDirectory = (Join-Path $PSScriptRoot 'app'))
$ErrorActionPreference = 'Stop'
$app = [IO.Path]::GetFullPath($AppDirectory)
$native = Join-Path $app 'AltServer.exe'
$worker = Join-Path $app 'altforge-remote.exe'
if (-not (Test-Path -LiteralPath $native) -or -not (Test-Path -LiteralPath $worker)) { throw 'Modified server and remote worker are required.' }
$allNative = @(Get-Process -Name AltServer -ErrorAction SilentlyContinue)
if (@($allNative | Where-Object { $_.Path -ne $native }).Count -gt 0) {
    throw 'Close the stock AltServer and earlier modified versions before starting this preview. No running process was stopped.'
}
$workers = @(Get-CimInstance Win32_Process -Filter "Name='altforge-remote.exe'" -ErrorAction SilentlyContinue)
$existing = @($workers | Where-Object { $_.ExecutablePath -eq $worker -and $_.CommandLine -match '\s"?serve"?\s*$' })
if ($existing.Count -eq 1) { Write-Host 'Persistent bridge is already running.'; return }
if ($workers.Count -gt 0) { throw 'A remote worker is busy or another version is running. It was not stopped.' }
$nativeProcess = $allNative | Where-Object { $_.Path -eq $native }
if (-not $nativeProcess) { Start-Process -FilePath $native -WindowStyle Hidden; Start-Sleep -Seconds 3 }
$state = Join-Path $env:LOCALAPPDATA 'AltServerDirectIPA\Remote'
Start-Process -FilePath $worker -ArgumentList @('serve') -WindowStyle Hidden -WorkingDirectory $app `
    -RedirectStandardOutput (Join-Path $state 'server.log') -RedirectStandardError (Join-Path $state 'error.log')
Write-Host 'Remote controller started. Keep this Windows user signed in and the PC awake.'
