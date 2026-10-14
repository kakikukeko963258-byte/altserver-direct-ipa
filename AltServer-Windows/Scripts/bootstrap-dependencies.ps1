[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
$dependenciesRoot = Join-Path $root "Dependencies"

$snapshots = @(
    @{
        Name = "libimobiledevice-vs"
        Path = "libimobiledevice-vs"
        Repository = "https://github.com/libimobiledevice-win32/libimobiledevice-vs.git"
        Revision = "d4016fabcf9be2c45255da7a7bda6a87a47df998"
    },
    @{
        Name = "libplist"
        Path = "libimobiledevice-vs\libplist"
        Repository = "https://github.com/libimobiledevice-win32/libplist.git"
        Revision = "1a9eab92b966f1b2f2ab41844721bf54bcc7a1a3"
    },
    @{
        Name = "libusbmuxd"
        Path = "libimobiledevice-vs\libusbmuxd"
        Repository = "https://github.com/libimobiledevice-win32/libusbmuxd.git"
        Revision = "ac86b23f57879b8b702f3712ba66729008d059a3"
    },
    @{
        Name = "libimobiledevice"
        Path = "libimobiledevice-vs\libimobiledevice"
        Repository = "https://github.com/libimobiledevice-win32/libimobiledevice.git"
        Revision = "0d4a7e905baeadafa098e629a5241fac6fbf7d24"
    },
    @{
        Name = "dirent"
        Path = "dirent"
        Repository = "https://github.com/tronkko/dirent.git"
        Revision = "c885633e126a3a949ec0497273ec13e2c03e862c"
    },
    @{
        Name = "mDNSResponder"
        Path = "mDNSResponder"
        Repository = "https://github.com/apple-oss-distributions/mDNSResponder.git"
        Revision = "d4658af3f5f291311c6aee4210aa6d39bda82bbe"
    }
)

function Invoke-Git {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
}

function Restore-Snapshot {
    param([Parameter(Mandatory = $true)][hashtable]$Snapshot)

    $destination = Join-Path $dependenciesRoot $Snapshot.Path
    if (Test-Path $destination) {
        if (-not (Test-Path (Join-Path $destination ".git"))) {
            $existingItem = Get-ChildItem -LiteralPath $destination -Force | Select-Object -First 1
            if ($existingItem) {
                throw "Dependency path exists but is not a Git checkout: $destination"
            }

            # A parent checkout can materialize an empty directory for an
            # uninitialized gitlink. It is safe to replace only that empty path.
            Remove-Item -LiteralPath $destination -Force
        }
        else {
            $head = (& git -C $destination rev-parse HEAD).Trim()
            if ($LASTEXITCODE -ne 0 -or $head -ne $Snapshot.Revision) {
                throw "Dependency $($Snapshot.Name) is not at pinned revision $($Snapshot.Revision)."
            }

            $changes = (& git -C $destination status --porcelain --untracked-files=no)
            if ($LASTEXITCODE -ne 0 -or $changes) {
                throw "Dependency $($Snapshot.Name) contains modified tracked files."
            }

            Write-Host "$($Snapshot.Name) is already at $head."
            return
        }
    }

    New-Item -ItemType Directory -Path $destination | Out-Null
    try {
        Invoke-Git -Arguments @("-C", $destination, "init", "--quiet")
        Invoke-Git -Arguments @("-C", $destination, "remote", "add", "origin", $Snapshot.Repository)

        $fetched = $false
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            & git -C $destination -c protocol.version=2 -c http.lowSpeedLimit=1024 -c http.lowSpeedTime=30 fetch --quiet --depth 1 origin $Snapshot.Revision
            if ($LASTEXITCODE -eq 0) {
                $fetched = $true
                break
            }

            if ($attempt -lt 3) {
                Start-Sleep -Seconds ([Math]::Pow(2, $attempt))
            }
        }

        if (-not $fetched) {
            throw "Could not fetch $($Snapshot.Name) after 3 attempts."
        }

        Invoke-Git -Arguments @("-C", $destination, "checkout", "--quiet", "--detach", "FETCH_HEAD")
        $head = (& git -C $destination rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or $head -ne $Snapshot.Revision) {
            throw "Fetched revision for $($Snapshot.Name) did not match the pin."
        }
    }
    catch {
        if (Test-Path $destination) {
            Remove-Item -LiteralPath $destination -Recurse -Force
        }
        throw
    }
}

New-Item -ItemType Directory -Path $dependenciesRoot -Force | Out-Null
foreach ($snapshot in $snapshots) {
    Restore-Snapshot -Snapshot $snapshot
}
