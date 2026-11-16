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
    # Pack builds every channel's installer and feed into velopack-output\ and
    # uploads nothing; Publish uploads what a Pack run left there. All does
    # both. The split lets create-release install, update, self-test and
    # uninstall the packed primary installer before anything is published
    # (TECH_DEBT_AUDIT TD-04, audit #467): the release used to go out with its
    # installer and hooks never executed.
    [ValidateSet("All", "Pack", "Publish")] [string] $Phase = "All",
    # Pack without downloading the previous release for delta generation: the
    # installer gate needs no delta and has no release token.
    [switch] $NoDelta,
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
    if (-not $variantEntry.Title) {
        throw "Variant $($artifact.Name) has no Title in simd-variants.psd1; the display name is release content"
    }
    $channel = $variantEntry.Channel
    $channelWork += [pscustomobject]@{
        ArtifactPath = $artifact.FullName
        Variant      = $variant
        Channel      = $channel
        Skipped      = $MissingChannels -notcontains $channel
        PackId       = Get-VelopackPackId -Channel $channel
        PackTitle    = $variantEntry.Title
        # The binaries are built with the v145 toolset (MSVC 14.5x), so the
        # installer must require the Visual C++ 2026 runtime (vpk: minimum
        # 14.50.35719, downloaded from aka.ms/vs/18). vcredist143 was satisfied
        # by any 14.3x runtime, with which a 14.4x+ build can crash in
        # std::mutex before its own install hook runs (audit #467, maintainer
        # question 4). The hook still tops the runtime up to the exact build
        # version (services/install/VcRuntime.cpp).
        Framework    = if ($variant -like "ARM64*" -or $variant -like "arm64*") { "vcredist145-arm64" } else { "vcredist145-x64" }
        PackDir      = Join-Path $WorkspaceRoot "velopack-input\$channel"
        OutputDir    = Join-Path $WorkspaceRoot "velopack-output\$channel"
    }
}

$plan = [pscustomobject]@{
    VpkVersion      = $manifest.Shared.VelopackVpkVersion
    VpkSha256       = $manifest.Shared.VelopackVpkSha256
    VpkPackageUrl   = "https://api.nuget.org/v3-flatcontainer/vpk/$($manifest.Shared.VelopackVpkVersion)/vpk.$($manifest.Shared.VelopackVpkVersion).nupkg"
    ReleaseName     = "EqualizerAPO-XT $PackVersion"
    RepoUrl         = "https://github.com/$Repository"
    Channels        = @($channelWork)
    NeedsSource     = [bool]$NeedsSource
    SourceZipPath   = Join-Path $WorkspaceRoot (Get-SourceZipAssetName -PackVersion $PackVersion)
    # Single source: Shared.QtPluginFolders in simd-variants.psd1, shared with
    # the Package-Artifacts.ps1 staging assertion (audit #275 TD-11).
    QtPluginFolders = @($manifest.Shared.QtPluginFolders)
    Phase           = $Phase
    Packs           = $Phase -ne "Publish"
    Publishes       = $Phase -ne "Pack"
    DownloadsDelta  = ($Phase -ne "Publish") -and -not $NoDelta
}
if ($PlanOnly) {
    return $plan
}

# vpk builds every installer. A version number alone would take whatever
# nuget.org serves under it; the package is downloaded, held to the manifest's
# SHA-256 and installed from that file alone (TECH_DEBT_AUDIT TD-69).
Import-Module (Join-Path $PSScriptRoot "Provisioning.psm1") -Force
$vpkSource = Join-Path $WorkspaceRoot "velopack-tool"
New-Item -ItemType Directory -Force -Path $vpkSource | Out-Null
$vpkPackage = Join-Path $vpkSource "vpk.$($plan.VpkVersion).nupkg"
Invoke-PinnedFileDownload -Url $plan.VpkPackageUrl -OutFile $vpkPackage -Sha256 $plan.VpkSha256
dotnet tool uninstall -g vpk 2>$null | Out-Null
$global:LASTEXITCODE = 0
dotnet tool install -g vpk --version $plan.VpkVersion --source $vpkSource
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
    if (-not $plan.Packs) { break }
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

    if ($plan.DownloadsDelta) {
        vpk download github `
            --repoUrl $plan.RepoUrl `
            --token $env:GITHUB_TOKEN `
            --outputDir $work.OutputDir `
            --channel $work.Channel
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "No previous Velopack release was downloaded for channel $($work.Channel). Continuing with a full package."
            $global:LASTEXITCODE = 0
        }
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
}

foreach ($work in $plan.Channels) {
    if (-not $plan.Publishes) { break }
    if ($work.Skipped) {
        if (-not $plan.Packs) { Write-Host "Channel $($work.Channel) is already complete on $Tag; skipping." }
        continue
    }
    # A Publish-only run uploads what an earlier Pack run of this job left in
    # the output folder; without it there is nothing to publish.
    if (-not (Test-Path (Join-Path $work.OutputDir (Get-SetupAssetName -Channel $work.Channel)))) {
        throw "Nothing packed for channel $($work.Channel) in $($work.OutputDir); run the Pack phase first."
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

if ($plan.NeedsSource -and $plan.Publishes) {
    gh release upload $Tag $plan.SourceZipPath --clobber --repo $Repository
    if ($LASTEXITCODE -ne 0) { throw "Source zip upload failed with exit code $LASTEXITCODE" }
}
