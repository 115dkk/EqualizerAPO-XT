[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Repository,
    [AllowEmptyString()] [string] $Tag,
    [Parameter(Mandatory)] [string] $Channel,
    [Parameter(Mandatory)] [string] $DownloadDirectory,
    [string] $GitHubEnvironmentPath,
    [switch] $SkipDownload,
    [switch] $SkipInstall,
    [switch] $SkipInstallRootResolution,
    [switch] $AllowMissing,
    [switch] $PassThru
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "ReleaseAssets.psm1") -Force
$asset = Get-SetupAssetName -Channel $Channel
New-Item -ItemType Directory -Force -Path $DownloadDirectory | Out-Null

if (-not $SkipDownload) {
    $releaseArgs = @("release", "download")
    if (-not [string]::IsNullOrWhiteSpace($Tag)) { $releaseArgs += $Tag }
    $releaseArgs += @("--repo", $Repository, "--pattern", $asset,
        "--dir", $DownloadDirectory, "--clobber")
    gh @releaseArgs
    if ($LASTEXITCODE -ne 0) { throw "Could not download $asset" }

    $sumArgs = @("release", "download")
    if (-not [string]::IsNullOrWhiteSpace($Tag)) { $sumArgs += $Tag }
    $sumArgs += @("--repo", $Repository, "--pattern", (Get-ChecksumsAssetName),
        "--dir", $DownloadDirectory, "--clobber")
    gh @sumArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "No SHA256SUMS.txt asset on the release; verification is unavailable"
        $global:LASTEXITCODE = 0
    }
}

$setup = Join-Path $DownloadDirectory $asset
if (-not (Test-Path -LiteralPath $setup)) {
    if ($AllowMissing) {
        Write-Warning "Setup not available: $asset"
        return
    }
    throw "Could not find $asset"
}

$sums = Join-Path $DownloadDirectory (Get-ChecksumsAssetName)
if (Test-Path -LiteralPath $sums) {
    $line = Get-Content -LiteralPath $sums |
        Where-Object { $_ -match ("  " + [regex]::Escape($asset) + '$') } |
        Select-Object -First 1
    if (-not $line) {
        throw "SHA256SUMS.txt does not list $asset"
    }
    $expected = ($line -split '\s+')[0].ToLowerInvariant()
    $actual = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -ne $actual) {
        throw "Checksum mismatch for $asset (expected $expected, got $actual)"
    }
    Write-Host "Checksum OK for $asset"
}
else {
    Write-Warning "No SHA256SUMS.txt asset on the release; verification is unavailable"
}

if (-not $SkipInstall) {
    $process = Start-Process -FilePath $setup -ArgumentList "--silent" -PassThru -Wait
    if ($process.ExitCode -ne 0) {
        throw "Setup exited with $($process.ExitCode)"
    }
}

$root = $null
$current = $null
if (-not $SkipInstallRootResolution) {
    $packId = Get-VelopackPackId -Channel $Channel
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA $packId),
        (Join-Path $env:ProgramData $packId)
    )
    $root = $candidates |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ "Update.exe") } |
        Select-Object -First 1
    if (-not $root) {
        $root = Get-ChildItem -LiteralPath $env:LOCALAPPDATA -Directory -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Name -like "EqualizerAPO-XT*" -and
                (Test-Path -LiteralPath (Join-Path $_.FullName "Update.exe"))
            } |
            Select-Object -ExpandProperty FullName -First 1
    }
    if (-not $root) { throw "Velopack install root not found after install" }
    $current = Join-Path $root "current"
    if ($GitHubEnvironmentPath) {
        "VELO_ROOT=$root" | Out-File -FilePath $GitHubEnvironmentPath -Append -Encoding utf8
        "VELO_CURRENT=$current" | Out-File -FilePath $GitHubEnvironmentPath -Append -Encoding utf8
    }
}

if ($PassThru) {
    [pscustomobject]@{
        SetupPath = $setup
        InstallRoot = $root
        CurrentDirectory = $current
    }
}
