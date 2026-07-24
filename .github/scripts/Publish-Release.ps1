[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Repository,
    [Parameter(Mandatory)] [string] $Tag,
    [Parameter(Mandatory)] [string] $PackVersion,
    [string] $ManifestPath = (Join-Path $PSScriptRoot "..\simd-variants.psd1"),
    [string[]] $AssetNames,
    [AllowEmptyString()] [string] $ReleaseNotes,
    [switch] $PassThru
)

$ErrorActionPreference = "Stop"

if (-not $PSBoundParameters.ContainsKey("AssetNames")) {
    $releaseJson = gh release view $Tag --repo $Repository --json assets,body 2>$null
    if ($LASTEXITCODE -eq 0) {
        $release = $releaseJson | ConvertFrom-Json
        $AssetNames = @($release.assets.name)
        $ReleaseNotes = [string]$release.body
        $releaseExists = $true
    }
    else {
        $global:LASTEXITCODE = 0
        $AssetNames = @()
        $ReleaseNotes = ""
        $releaseExists = $false
    }
}
else {
    $releaseExists = $AssetNames.Count -gt 0 -or -not [string]::IsNullOrWhiteSpace($ReleaseNotes)
}

$manifest = Import-PowerShellDataFile $ManifestPath
$missingChannels = @()
foreach ($channel in @($manifest.Variants.Channel)) {
    $setup = "EqualizerAPO-XT-$channel-$channel-Setup.exe"
    $feed = "releases.$channel.json"
    if ($AssetNames -notcontains $setup -or $AssetNames -notcontains $feed) {
        $missingChannels += $channel
    }
}

$sourceName = "EqualizerAPO-XT-source-$PackVersion.zip"
$needsSource = $AssetNames -notcontains $sourceName
$needsInstaller = $AssetNames -notcontains "EqualizerAPO-XT-Setup.exe"
$releaseShapeChanged = $missingChannels.Count -gt 0 -or $needsSource -or $needsInstaller
$needsChecksums = $releaseShapeChanged -or $AssetNames -notcontains "SHA256SUMS.txt"
$needsNotes = $releaseShapeChanged -or [string]::IsNullOrWhiteSpace($ReleaseNotes)
$complete = -not $needsChecksums -and -not $needsNotes

$plan = [pscustomobject]@{
    ReleaseExists  = $releaseExists
    Complete       = $complete
    MissingChannels = @($missingChannels)
    NeedsSource    = $needsSource
    NeedsInstaller = $needsInstaller
    NeedsChecksums = $needsChecksums
    NeedsNotes     = $needsNotes
}

Write-Host "Release $Tag plan: complete=$($plan.Complete), missing channels=$($missingChannels -join ',')"
if ($env:GITHUB_OUTPUT) {
    "release_exists=$($plan.ReleaseExists.ToString().ToLowerInvariant())" >> $env:GITHUB_OUTPUT
    "complete=$($plan.Complete.ToString().ToLowerInvariant())" >> $env:GITHUB_OUTPUT
    "missing_channels=$($missingChannels -join ',')" >> $env:GITHUB_OUTPUT
    "needs_source=$($plan.NeedsSource.ToString().ToLowerInvariant())" >> $env:GITHUB_OUTPUT
    "needs_installer=$($plan.NeedsInstaller.ToString().ToLowerInvariant())" >> $env:GITHUB_OUTPUT
    "needs_checksums=$($plan.NeedsChecksums.ToString().ToLowerInvariant())" >> $env:GITHUB_OUTPUT
    "needs_notes=$($plan.NeedsNotes.ToString().ToLowerInvariant())" >> $env:GITHUB_OUTPUT
}

if ($PassThru) {
    $plan
}
