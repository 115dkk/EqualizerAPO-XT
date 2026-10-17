# The release asset-name grammar, spelled once. Audit #250 F067: the
# EqualizerAPO-XT-<channel>-<channel>-Setup.exe shape (the channel appears
# twice because the Velopack pack id already embeds it and vpk appends the
# channel again), the universal installer, the feed, the source zip, the
# checksums file and the pack id used to live as parallel literals in two
# languages and six files. PowerShell consumers import this module; the C++
# consumer (Installer/AutoInstaller) includes release/ReleaseAssetNames.h, and
# ReleaseAssets.Tests.ps1 keeps the two spellings in step.

$script:ProductPrefix = "EqualizerAPO-XT"

function Get-VelopackPackId {
    param([Parameter(Mandatory)] [string] $Channel)
    "$script:ProductPrefix-$Channel"
}

function Get-SetupAssetName {
    param([Parameter(Mandatory)] [string] $Channel)
    "$(Get-VelopackPackId -Channel $Channel)-$Channel-Setup.exe"
}

function Get-UniversalSetupAssetName {
    "$script:ProductPrefix-Setup.exe"
}

function Get-FeedAssetName {
    param([Parameter(Mandatory)] [string] $Channel)
    "releases.$Channel.json"
}

function Get-SourceZipAssetName {
    param([Parameter(Mandatory)] [string] $PackVersion)
    "$script:ProductPrefix-source-$PackVersion.zip"
}

function Get-ChecksumsAssetName {
    "SHA256SUMS.txt"
}

# Picks the release whose auto-detect installer can be reused: the newest
# published (non-draft, non-prerelease) release that is not the one being
# assembled. $Releases is the gh release list order, newest first.
function Select-PreviousReleaseTag {
    param(
        [AllowEmptyCollection()] [object[]] $Releases,
        [Parameter(Mandatory)] [string] $CurrentTag
    )
    foreach ($release in @($Releases)) {
        if ($release.tagName -eq $CurrentTag) { continue }
        if ($release.isDraft -or $release.isPrerelease) { continue }
        return $release.tagName
    }
    return $null
}

# The SHA256SUMS.txt body Installer/AutoInstaller.cpp parses:
# sha256sum-compatible lines (lowercase hex, two spaces, name), LF, sorted
# by name, with a final LF. Audit #422 TD-43 moved it here from
# New-ReleaseChecksums.ps1 so Pester can import it instead of cutting it out
# of the script's source.
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

Export-ModuleMember -Function Get-VelopackPackId, Get-SetupAssetName,
Get-UniversalSetupAssetName, Get-FeedAssetName, Get-SourceZipAssetName,
Get-ChecksumsAssetName, Select-PreviousReleaseTag, Format-ChecksumLines
