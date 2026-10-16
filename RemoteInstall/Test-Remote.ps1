param([string]$AppDirectory = (Join-Path $PSScriptRoot 'app'))
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'AltServerDirectIPA\Remote\config.json'))) { throw 'Run Setup-Remote first.' }
& (Join-Path $PSScriptRoot 'Start-Remote.ps1') -AppDirectory $AppDirectory
$worker = Join-Path ([IO.Path]::GetFullPath($AppDirectory)) 'altforge-remote.exe'
Write-Host 'Keep iPhone on Wi-Fi and VPN while the persistent connection is acquired.'
$timer = [Diagnostics.Stopwatch]::StartNew()
while ($timer.Elapsed.TotalSeconds -lt 90) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $worker
    $info.Arguments = 'probe'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $probe = [Diagnostics.Process]::new()
    $probe.StartInfo = $info
    try {
        if (-not $probe.Start()) { throw 'Could not start connection check.' }
        $remaining = [Math]::Min(45000,[Math]::Max(1,90000-[int]$timer.Elapsed.TotalMilliseconds))
        if ($probe.WaitForExit($remaining)) {
            if ($probe.ExitCode -eq 0) { Write-Host 'Registered iPhone verified through the persistent VPN bridge.'; return }
        } else { $probe.Kill(); $probe.WaitForExit() }
    } finally { $probe.Dispose() }
    Start-Sleep -Seconds 3
}
throw 'Bridge not ready. Connect iPhone to Wi-Fi and VPN, then try again. No installation was started.'
