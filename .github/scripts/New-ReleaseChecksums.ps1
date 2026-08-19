<#
.SYNOPSIS
    Builds and uploads SHA256SUMS.txt for a release's setup executables.

.DESCRIPTION
    Extracted from the inline "Publish release checksums" step in build.yml
    (audit #275 TD-24): the auto-detect installer refuses to run a downloaded
    per-channel Setup.exe whose SHA-256 does not match this file
    (Installer/AutoInstaller.cpp), so the file must list every *-Setup.exe
    asset in the exact format the parser reads - and the parser was tested
    while the writer was not. Format-ChecksumLines is the pure writer half
    Pester pins: sha256sum-compatible lines, lowercase hex, two spaces, LF,
    sorted by name, trailing newline.

    The assets are re-downloaded from the release rather than hashed from
    local build directories so the hashes cover exactly the bytes users get.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Repository,
    [Parameter(Mandatory)] [string] $Tag,
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "ReleaseAssets.psm1") -Force

function Format-ChecksumLines {
    param(
        # Hashtable of asset name -> SHA-256 hex (any case).
        [Parameter(Mandatory)] [hashtable] $Hashes
    )
    $lines = foreach ($name in ($Hashes.Keys | Sort-Object)) {
        "$($Hashes[$name].ToLowerInvariant())  $name"
    }
    (($lines -join "`n") + "`n")
}

if ($PlanOnly) {
    return [pscustomobject]@{
        ChecksumsAssetName = Get-ChecksumsAssetName
        SumsPath           = Join-Path $WorkspaceRoot (Get-ChecksumsAssetName)
        SetupAssetPattern  = '-Setup\.exe$'
    }
}

$assetNames = @(gh release view $Tag --repo $Repository --json assets --jq '.assets[].name' |
    Where-Object { $_ -match '-Setup\.exe$' })
if ($assetNames.Count -eq 0) {
    throw "No setup executables found on release $Tag; nothing to checksum."
}

$downloadDir = Join-Path $WorkspaceRoot "checksum-input"
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null

$hashes = @{}
foreach ($name in $assetNames) {
    gh release download $Tag --repo $Repository --pattern $name --dir $downloadDir --clobber
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download release asset $name for checksumming"
    }
    $hash = (Get-FileHash -Path (Join-Path $downloadDir $name) -Algorithm SHA256).Hash
    $hashes[$name] = $hash
    Write-Host "$($hash.ToLowerInvariant())  $name"
}

$sumsPath = Join-Path $WorkspaceRoot (Get-ChecksumsAssetName)
[System.IO.File]::WriteAllText($sumsPath, (Format-ChecksumLines -Hashes $hashes))

gh release upload $Tag $sumsPath --clobber --repo $Repository
if ($LASTEXITCODE -ne 0) {
    throw "Failed to upload $(Get-ChecksumsAssetName) to release $Tag"
}
