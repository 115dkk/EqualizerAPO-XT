<#
Reuses the previous release's auto-detect installer when nothing that goes
into that binary has changed.

Why: EqualizerAPO-XT-Setup.exe is deliberately release-agnostic (it always
resolves /releases/latest), yet every release used to rebuild and upload a
byte-different copy. Defender/SmartScreen reputation is accumulated per file
hash, and an unsigned binary whose hash resets every release starts every
release at zero - the root of the Wacatac.B!ml false-positive reports. As
long as the installer's inputs are untouched, re-uploading the previous
release's exact bytes lets one hash keep collecting reputation.

The decision is deliberately conservative: any anomaly (no previous release,
missing asset, unfetchable tag, changed watch path) falls back to a fresh
build, which is always correct - just reputation-less.

version.h is intentionally NOT watched: it only stamps the version resource
(and the repository slug, which never changes without touching the watched
grammar anyway). A reused binary therefore reports the version of the release
that last changed the installer - that is its real version.

Returns a pscustomobject: Reuse (bool), PreviousTag, Reason. On Reuse=true
the previous asset has been downloaded to OutputPath.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Repository,
    [Parameter(Mandatory)] [string] $Tag,
    [Parameter(Mandatory)] [string] $ReleaseSha,
    [Parameter(Mandatory)] [string] $AssetName,
    [Parameter(Mandatory)] [string] $OutputPath,
    # Everything the installer binary is built from. AutoInstaller's includes
    # outside its own directory are the release grammar, the shared Win32
    # helpers and the Editor icon its .rc embeds.
    [string[]] $WatchPaths = @(
        "Installer",
        "release",
        "platform/windows",
        "Editor/icons/applications-graphics-rotated.ico"
    )
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "ReleaseAssets.psm1") -Force

function New-Decision {
    param([bool] $Reuse, [string] $PreviousTag, [string] $Reason)
    [pscustomobject]@{ Reuse = $Reuse; PreviousTag = $PreviousTag; Reason = $Reason }
}

$releasesJson = gh release list --repo $Repository --limit 15 --json tagName,isDraft,isPrerelease
if ($LASTEXITCODE -ne 0) {
    return New-Decision $false "" "could not list releases"
}
$previousTag = Select-PreviousReleaseTag -Releases ($releasesJson | ConvertFrom-Json) -CurrentTag $Tag
if (-not $previousTag) {
    return New-Decision $false "" "no previous published release"
}

$assetsJson = gh release view $previousTag --repo $Repository --json assets
if ($LASTEXITCODE -ne 0) {
    return New-Decision $false $previousTag "could not read the previous release's assets"
}
$assetNames = @(($assetsJson | ConvertFrom-Json).assets.name)
if ($assetNames -notcontains $AssetName) {
    return New-Decision $false $previousTag "previous release has no $AssetName"
}

# The checkout is shallow; bring in the previous tag's tree for the diff.
git fetch --quiet --depth=1 origin "refs/tags/${previousTag}:refs/tags/${previousTag}" 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
    return New-Decision $false $previousTag "could not fetch tag $previousTag"
}

git diff --quiet $previousTag $ReleaseSha -- @WatchPaths
if ($LASTEXITCODE -eq 1) {
    $global:LASTEXITCODE = 0
    return New-Decision $false $previousTag "installer inputs changed since $previousTag"
}
if ($LASTEXITCODE -ne 0) {
    return New-Decision $false $previousTag "git diff against $previousTag failed"
}

if (Test-Path $OutputPath) { Remove-Item $OutputPath -Force }
gh release download $previousTag --repo $Repository --pattern $AssetName --output $OutputPath
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $OutputPath)) {
    return New-Decision $false $previousTag "could not download $AssetName from $previousTag"
}

return New-Decision $true $previousTag "installer inputs unchanged since $previousTag"
