<#
.SYNOPSIS
    Packages and uploads the per-channel Velopack releases (and the source
    zip) for a tag, resuming any channels a previous attempt left missing.

.DESCRIPTION
    Extracted from the inline "Create Velopack release" step in build.yml
    (audit #250 F068): the 159 lines that actually create a release were the
    last release-critical code outside the script + Pester pattern, so their
    errors could only be discovered on release day.

    The plan phase is pure: it enumerates the downloaded build artifacts,
    resolves each to its manifest variant (audit F066 - the artifact name is
    "<platform>-<simd>"; nothing re-derives channels by string convention),
    filters against the missing-channel resume list from Publish-Release.ps1,
    and answers what would be packed under which identity. -PlanOnly returns
    that plan for Pester. Execution stages each channel's payload (Qt plugin
    folders under qt\ because Editor.exe calls addLibraryPath("qt"), bundled
    configs, doc shortcuts), downloads the previous release for delta
    generation (a miss downgrades to a full package), then vpk pack/upload
    and, when asked, uploads the source zip.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Repository,
    [Parameter(Mandatory)] [string] $Tag,
    [Parameter(Mandatory)] [string] $PackVersion,
    [Parameter(Mandatory)] [string] $TargetCommit,
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [string] $InputRoot,
    [string[]] $MissingChannels = @(),
    [switch] $NeedsSource,
    [string] $ManifestPath,
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "ReleaseAssets.psm1") -Force

if (-not $InputRoot) { $InputRoot = Join-Path $WorkspaceRoot "release-input" }
if (-not $ManifestPath) { $ManifestPath = Join-Path $PSScriptRoot "..\simd-variants.psd1" }
$manifest = Import-PowerShellDataFile $ManifestPath

$buildArtifacts = @(Get-ChildItem -Path $InputRoot -Directory -Filter "EqualizerAPO-*" -ErrorAction SilentlyContinue)
if ($buildArtifacts.Count -eq 0) {
    throw "No build artifacts were found in $InputRoot"
}

$channelWork = @()
foreach ($artifact in $buildArtifacts) {
    $variant = $artifact.Name.Substring("EqualizerAPO-".Length)
    $sep = $variant.IndexOf('-')
    $variantPlatform = $variant.Substring(0, $sep)
    $variantSimd = $variant.Substring($sep + 1)
    $variantEntry = $manifest.Variants |
        Where-Object { $_.Platform -eq $variantPlatform -and $_.Simd -eq $variantSimd } |
        Select-Object -First 1
    if (-not $variantEntry) {
        throw "Artifact $($artifact.Name) has no matching variant in simd-variants.psd1"
    }
    $channel = $variantEntry.Channel
    $channelWork += [pscustomobject]@{
        ArtifactPath = $artifact.FullName
        Variant      = $variant
        Channel      = $channel
        Skipped      = $MissingChannels -notcontains $channel
        PackId       = Get-VelopackPackId -Channel $channel
        PackTitle    = "EqualizerAPO-XT $variant"
        Framework    = if ($variant -like "ARM64*" -or $variant -like "arm64*") { "vcredist143-arm64" } else { "vcredist143-x64" }
        PackDir      = Join-Path $WorkspaceRoot "velopack-input\$channel"
        OutputDir    = Join-Path $WorkspaceRoot "velopack-output\$channel"
    }
}

$plan = [pscustomobject]@{
    VpkVersion      = $manifest.Shared.VelopackVpkVersion
    ReleaseName     = "EqualizerAPO-XT $PackVersion"
    RepoUrl         = "https://github.com/$Repository"
    Channels        = @($channelWork)
    NeedsSource     = [bool]$NeedsSource
    SourceZipPath   = Join-Path $WorkspaceRoot (Get-SourceZipAssetName -PackVersion $PackVersion)
    # Single source: Shared.QtPluginFolders in simd-variants.psd1, shared with
    # the Package-Artifacts.ps1 staging assertion (audit #275 TD-11).
    QtPluginFolders = @($manifest.Shared.QtPluginFolders)
}
if ($PlanOnly) {
    return $plan
}

dotnet tool install -g vpk --version $plan.VpkVersion
if ($LASTEXITCODE -ne 0) { throw "dotnet tool install vpk $($plan.VpkVersion) failed with exit code $LASTEXITCODE" }
$env:PATH = "$env:USERPROFILE\.dotnet\tools;$env:PATH"

# Required payload. These used to sit behind Test-Path guards, so a moved
# Setup\config or icon shipped a package silently missing its sample configs
# (audit #275 TD-11): every file below is release content, so a miss is an
# error, not a skip.
$iconPath = Join-Path $WorkspaceRoot "Editor\icons\app-icon.ico"
$configSource = Join-Path $WorkspaceRoot "Setup\config"
$extrasSource = @(
    (Join-Path $WorkspaceRoot "Setup\Configuration tutorial (online).url"),
    (Join-Path $WorkspaceRoot "Setup\Configuration reference (online).url")
)
foreach ($required in (@($iconPath, $configSource) + $extrasSource)) {
    if (-not (Test-Path $required)) {
        throw "Required release payload is missing: $required"
    }
}
$qtPluginFolders = $plan.QtPluginFolders
$mainExe = "Editor.exe"

foreach ($work in $plan.Channels) {
    if ($work.Skipped) {
        Write-Host "Channel $($work.Channel) is already complete on $Tag; skipping."
        continue
    }

    New-Item -ItemType Directory -Force -Path $work.OutputDir | Out-Null
    if (Test-Path $work.PackDir) {
        Remove-Item -Path $work.PackDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $work.PackDir | Out-Null

    Write-Host "`n=== Preparing Velopack pack dir for $($work.Variant) ==="
    # Copy everything from the build artifact (binaries + Qt deps at top level)
    Copy-Item -Path (Join-Path $work.ArtifactPath "*") -Destination $work.PackDir -Recurse -Force

    # Editor.exe calls addLibraryPath("qt"), so Qt plugin folders must live under qt\
    $qtTarget = Join-Path $work.PackDir "qt"
    New-Item -ItemType Directory -Force -Path $qtTarget | Out-Null
    foreach ($folder in $qtPluginFolders) {
        $source = Join-Path $work.PackDir $folder
        if (Test-Path $source) {
            $dest = Join-Path $qtTarget $folder
            if (Test-Path $dest) {
                Remove-Item -Path $dest -Recurse -Force
            }
            Move-Item -Path $source -Destination $dest -Force
            Write-Host "Relocated $folder -> qt\$folder"
        }
    }

    # Bundle the sample configs and shortcuts (existence asserted above)
    $configTarget = Join-Path $work.PackDir "config"
    New-Item -ItemType Directory -Force -Path $configTarget | Out-Null
    Copy-Item -Path (Join-Path $configSource "*") -Destination $configTarget -Recurse -Force
    foreach ($extra in $extrasSource) {
        Copy-Item -Path $extra -Destination $work.PackDir -Force
    }

    if (-not (Test-Path (Join-Path $work.PackDir $mainExe))) {
        throw "$mainExe not found in $($work.PackDir)"
    }

    vpk download github `
        --repoUrl $plan.RepoUrl `
        --token $env:GITHUB_TOKEN `
        --outputDir $work.OutputDir `
        --channel $work.Channel
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "No previous Velopack release was downloaded for channel $($work.Channel). Continuing with a full package."
        $global:LASTEXITCODE = 0
    }

    $packArgs = @(
        'pack',
        '--packId', $work.PackId,
        '--packVersion', $PackVersion,
        '--packDir', $work.PackDir,
        '--mainExe', $mainExe,
        '--packTitle', $work.PackTitle,
        '--packAuthors', 'EqualizerAPO-XT contributors',
        '--outputDir', $work.OutputDir,
        '--channel', $work.Channel,
        '--framework', $work.Framework,
        '--noPortable',
        '--skipVeloAppCheck'
    )
    $packArgs += @('--icon', $iconPath)

    Write-Host "vpk $($packArgs -join ' ')"
    vpk @packArgs
    if ($LASTEXITCODE -ne 0) { throw "vpk pack failed for channel $($work.Channel) with exit code $LASTEXITCODE" }

    # The asset-name grammar (ReleaseAssets.psm1) rests on the observation
    # that vpk names its outputs <packId>-<channel>-Setup.exe and
    # releases.<channel>.json. Publish-Release.ps1's missing-channel resume
    # matches release assets against that grammar, so if a VelopackVpkVersion
    # bump ever changes the naming, every channel would read as missing forever.
    # Assert the contract right where the files are produced (audit #275
    # TD-12) instead.
    foreach ($expected in @((Get-SetupAssetName -Channel $work.Channel),
                            (Get-FeedAssetName -Channel $work.Channel))) {
        if (-not (Test-Path (Join-Path $work.OutputDir $expected))) {
            throw ("vpk pack did not produce '$expected' in $($work.OutputDir). " +
                "The vpk naming convention has drifted from ReleaseAssets.psm1; " +
                "align the grammar before releasing.")
        }
    }

    vpk upload github `
        --repoUrl $plan.RepoUrl `
        --token $env:GITHUB_TOKEN `
        --outputDir $work.OutputDir `
        --channel $work.Channel `
        --publish `
        --merge `
        --releaseName $plan.ReleaseName `
        --tag $Tag `
        --targetCommitish $TargetCommit
    if ($LASTEXITCODE -ne 0) { throw "vpk upload failed for channel $($work.Channel) with exit code $LASTEXITCODE" }
}

if ($plan.NeedsSource) {
    gh release upload $Tag $plan.SourceZipPath --clobber --repo $Repository
    if ($LASTEXITCODE -ne 0) { throw "Source zip upload failed with exit code $LASTEXITCODE" }
}
