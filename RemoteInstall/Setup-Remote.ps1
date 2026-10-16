param(
    [string]$PhoneVpnIP,
    [string]$PcVpnIP,
    [string]$AppDirectory = (Join-Path $PSScriptRoot 'app'),
    [switch]$UseTrustedWiFi
)
$ErrorActionPreference = 'Stop'
if (-not $PhoneVpnIP -or -not $PcVpnIP) {
    $tailscale = Join-Path $env:ProgramFiles 'Tailscale\tailscale.exe'
    if (-not (Test-Path -LiteralPath $tailscale)) { throw 'Specify both VPN IPv4 addresses, or install and connect Tailscale.' }
    $status = (& $tailscale status --json 2>$null | ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0 -or $status.BackendState -ne 'Running') { throw 'Connect Tailscale first.' }
    $phones = @($status.Peer.PSObject.Properties.Value | Where-Object { $_.OS -eq 'iOS' -and $_.Online })
    if ($phones.Count -ne 1) { throw 'Keep one iPhone online in Tailscale, or explicitly specify its VPN address.' }
    $PhoneVpnIP = $phones[0].TailscaleIPs | Where-Object { $_ -match '^100\.' } | Select-Object -First 1
    $PcVpnIP = $status.TailscaleIPs | Where-Object { $_ -match '^100\.' } | Select-Object -First 1
    if (-not $PhoneVpnIP -or -not $PcVpnIP) { throw 'Tailscale IPv4 addresses unavailable.' }
}

$worker = Join-Path ([IO.Path]::GetFullPath($AppDirectory)) 'altforge-remote.exe'
if (-not (Test-Path -LiteralPath $worker -PathType Leaf)) { throw 'Remote worker missing.' }
$binding = @(Get-NetIPAddress -IPAddress $PcVpnIP -AddressFamily IPv4 -ErrorAction SilentlyContinue)
if ($binding.Count -ne 1) { throw 'PC VPN address is not assigned. Connect the VPN first.' }
$adapter = Get-NetAdapter -InterfaceIndex $binding[0].InterfaceIndex
if (($adapter.Name + ' ' + $adapter.InterfaceDescription) -notmatch 'Tailscale|WireGuard') {
    throw 'Select the PC address assigned to Tailscale or WireGuard, not a normal LAN adapter.'
}
$folder = Join-Path $env:LOCALAPPDATA 'AltServerDirectIPA\Remote'
if (Test-Path -LiteralPath (Join-Path $folder 'config.json')) { throw 'A registration already exists. It was not changed.' }
[IO.Directory]::CreateDirectory($folder) | Out-Null
# Protect only this app's new state directory. Do not change system permissions.
$user = [Security.Principal.WindowsIdentity]::GetCurrent().User
$system = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
# Change the DACL only; Set-Acl can also request privileged audit/owner access.
& "$env:SystemRoot\System32\icacls.exe" $folder /grant:r "*$($user.Value):(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' /inheritance:r /q | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Could not protect the private registration directory.' }
$acl = Get-Acl -LiteralPath $folder
$rules = @($acl.GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier]))
if (-not $acl.AreAccessRulesProtected -or @($rules | Where-Object { $_.IdentityReference.Value -notin @($user.Value,$system.Value) }).Count -gt 0) {
    throw 'The registration directory has unexpected permissions; no pairing was saved.'
}
$approved = Join-Path $folder 'ApprovedIPAs'
[IO.Directory]::CreateDirectory($approved) | Out-Null
$mode = if ($UseTrustedWiFi) { 'pair-wifi' } else { 'pair' }
& $worker $mode $PhoneVpnIP $PcVpnIP $approved
if ($LASTEXITCODE -ne 0) { throw 'USB registration failed. Leave exactly one unlocked iPhone connected and enable Developer Mode.' }
$verb = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Classes\SystemFileAssociations\.ipa\shell\AltForgeRemoteIPA')
try {
    $verb.SetValue('', 'AltForge: 外出先のiPhone用に用意')
    $command = $verb.CreateSubKey('command')
    try { $command.SetValue('', ('"' + (Join-Path ([IO.Path]::GetFullPath($AppDirectory)) 'AltServer.exe') + '" --queue-remote "%1"')) }
    finally { $command.Dispose() }
} finally { $verb.Dispose() }
Write-Host 'Registration saved. Connect both devices to the VPN, then run Test-Remote.ps1.'
