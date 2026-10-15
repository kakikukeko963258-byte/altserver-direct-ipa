param([string]$AppDirectory = (Join-Path $PSScriptRoot 'app'))
$ErrorActionPreference = 'Stop'
$app = [IO.Path]::GetFullPath($AppDirectory)
$native = Join-Path $app 'AltServer.exe'
$worker = Join-Path $app 'altforge-remote.exe'
if (-not (Test-Path -LiteralPath $native) -or -not (Test-Path -LiteralPath $worker)) { throw 'Modified server and remote worker are required.' }
if (Get-Process -Name 'altforge-remote' -ErrorAction SilentlyContinue) { throw 'A remote worker is already running. Check it before starting another.' }
$nativeProcess = Get-Process -Name AltServer -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $native }
if (-not $nativeProcess) { Start-Process -FilePath $native -WindowStyle Hidden; Start-Sleep -Seconds 3 }
$state = Join-Path $env:LOCALAPPDATA 'AltServerDirectIPA\Remote'
Start-Process -FilePath $worker -ArgumentList @('serve') -WindowStyle Hidden -WorkingDirectory $app `
    -RedirectStandardOutput (Join-Path $state 'server.log') -RedirectStandardError (Join-Path $state 'error.log')
Write-Host 'Remote controller started. Keep this Windows user signed in and the PC awake.'
