param([string]$AppDirectory = (Join-Path $PSScriptRoot 'app'))
$ErrorActionPreference = 'Stop'
& (Join-Path ([IO.Path]::GetFullPath($AppDirectory)) 'altforge-remote.exe') probe
if ($LASTEXITCODE -ne 0) { throw 'Cannot verify the registered iPhone over VPN. Installation was not started.' }
