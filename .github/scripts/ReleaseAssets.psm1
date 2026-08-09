# The release asset-name grammar, spelled once. Audit #250 F067: the
# EqualizerAPO-XT-<channel>-<channel>-Setup.exe shape (the channel appears
# twice because the Velopack pack id already embeds it and vpk appends the
# channel again), the universal installer, the feed, the source zip, the
# checksums file and the pack id used to live as parallel literals in two
# languages and six files. PowerShell consumers import this module; the C++
# consumers (Installer/AutoInstaller, UpdateChecker) include
# release/ReleaseAssetNames.h, and ReleaseAssets.Tests.ps1 keeps the two
# spellings in step.

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

Export-ModuleMember -Function Get-VelopackPackId, Get-SetupAssetName,
Get-UniversalSetupAssetName, Get-FeedAssetName, Get-SourceZipAssetName,
Get-ChecksumsAssetName
