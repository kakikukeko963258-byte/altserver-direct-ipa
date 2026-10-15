param(
    [Parameter(Mandatory=$true)][string]$PhoneVpnIP,
    [Parameter(Mandatory=$true)][string]$PcVpnIP,
    [string]$AppDirectory = (Join-Path $PSScriptRoot 'app')
)
$ErrorActionPreference = 'Stop'
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
$acl = [Security.AccessControl.DirectorySecurity]::new()
$acl.SetAccessRuleProtection($true,$false)
$user = [Security.Principal.WindowsIdentity]::GetCurrent().User
$system = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
foreach ($sid in @($user,$system)) {
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid,'FullControl','ContainerInherit,ObjectInherit','None','Allow'))
}
Set-Acl -LiteralPath $folder -AclObject $acl
$approved = Join-Path $folder 'ApprovedIPAs'
[IO.Directory]::CreateDirectory($approved) | Out-Null
& $worker pair $PhoneVpnIP $PcVpnIP $approved
if ($LASTEXITCODE -ne 0) { throw 'USB registration failed. Leave exactly one unlocked iPhone connected and enable Developer Mode.' }
$verb = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Classes\SystemFileAssociations\.ipa\shell\AltForgeRemoteIPA')
try {
    $verb.SetValue('', 'AltForge: 外出先のiPhone用に用意')
    $command = $verb.CreateSubKey('command')
    try { $command.SetValue('', ('"' + (Join-Path ([IO.Path]::GetFullPath($AppDirectory)) 'AltServer.exe') + '" --queue-remote "%1"')) }
    finally { $command.Dispose() }
} finally { $verb.Dispose() }
Write-Host 'Registration saved. Connect both devices to the VPN, then run Test-Remote.ps1.'
