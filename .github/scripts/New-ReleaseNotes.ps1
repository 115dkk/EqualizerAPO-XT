param(
  [Parameter(Mandatory = $true)]
  [string]$Repository,

  [Parameter(Mandatory = $true)]
  [string]$Tag,

  [Parameter(Mandatory = $true)]
  [string]$PackVersion,

  [Parameter(Mandatory = $true)]
  [string]$WorkflowRunId,

  [Parameter(Mandatory = $true)]
  [string]$TargetCommit,

  [Parameter(Mandatory = $true)]
  [string]$OutputPath
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "ReleaseAssets.psm1") -Force

# Release-channel table for the SIMD/architecture builds. The channel KEYS are the
# single source of truth in .github/simd-variants.psd1 (Variants[].Channel); the
# Pattern / SortOrder / Guidance columns are release-notes-specific and stay here.
# Order matters: Get-ChannelFromAssetName matches Pattern values top to bottom,
# so more specific patterns (e.g. avx10/avx512/avx2) must precede broader ones (avx).
$ChannelTable = [ordered]@{
  "arm64-neon"  = @{ Pattern = "arm64";                      SortOrder = 40;  Guidance = "Windows on ARM64 devices." }
  "x64-sse2"    = @{ Pattern = "sse2|baseline";              SortOrder = 5;   Guidance = "Baseline 64-bit Intel/AMD systems. Pick this for older x64 CPUs that do not support AVX." }
  "x64-avx10-1" = @{ Pattern = "avx10-1|avx10_1";            SortOrder = 30;  Guidance = "64-bit Intel/AMD systems with AVX10.1 support. Note: CI compiles but cannot execute this instruction set on its hosted runners, so this build skips the runtime audio tests that the sse2/avx/avx2 builds pass." }
  "x64-avx512"  = @{ Pattern = "avx512";                     SortOrder = 20;  Guidance = "64-bit Intel/AMD systems where you specifically want the AVX-512 build. Note: CI compiles but cannot execute this instruction set on its hosted runners, so this build skips the runtime audio tests that the sse2/avx/avx2 builds pass." }
  "x64-avx2"    = @{ Pattern = "avx2";                       SortOrder = 10;  Guidance = "Most 64-bit Intel/AMD systems with AVX2. Use this if you are unsure which x64 build to pick." }
  "x64-avx"     = @{ Pattern = "(^|[-_.])avx($|[-_.])";      SortOrder = 8;   Guidance = "64-bit Intel/AMD systems with AVX, but not AVX2." }
}

# Fail loudly if the manifest's channel set drifts from this table, so a new or
# renamed variant cannot ship with missing download guidance. The manifest sits two
# levels up from this script (.github/scripts/ -> .github/simd-variants.psd1).
# $manifestChannels is reused below to author the Verification variant list from
# the manifest instead of hard-coding it here.
$manifestChannels = @()
$manifestPath = Join-Path $PSScriptRoot "..\simd-variants.psd1"
if (Test-Path $manifestPath) {
  $manifestChannels = @((Import-PowerShellDataFile -Path $manifestPath).Variants | ForEach-Object { $_.Channel })
  $missingFromTable = @($manifestChannels | Where-Object { -not $ChannelTable.Contains($_) })
  if ($missingFromTable.Count -gt 0) {
    throw "Channels in simd-variants.psd1 have no entry in `$ChannelTable: $($missingFromTable -join ', '). Add them (with Pattern/SortOrder/Guidance) before releasing."
  }
}

$UnknownChannelSortOrder = 100
$UnknownChannelGuidance = "Special-purpose asset. Use one of the setup executables for normal installation."

# The architecture-agnostic front-door installer (docs/AutoDetectInstaller.md).
# It is not a channel: it detects the CPU at install time and pulls the matching
# per-channel build. Featured first in the download table.
$UniversalInstallerName = Get-UniversalSetupAssetName
$UniversalInstallerSortOrder = -1
$UniversalInstallerGuidance = "Recommended. Detects your CPU (architecture and AVX level) and installs the matching build automatically. Use this unless you have a reason to pick a specific build below."

function Invoke-GhJson {
  param(
    [Parameter(Mandatory = $true)]
    [string[]]$Arguments
  )

  $output = & gh @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "gh $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
  }

  $json = ($output | Out-String).Trim()
  if ([string]::IsNullOrWhiteSpace($json)) {
    return $null
  }

  $result = $json | ConvertFrom-Json
  if ($result -is [System.Array]) {
    foreach ($item in $result) {
      $item
    }
    return
  }

  return $result
}

function Get-ChannelFromAssetName {
  param(
    [Parameter(Mandatory = $true)]
    [string]$AssetName
  )

  $lowerName = $AssetName.ToLowerInvariant()
  foreach ($channel in $ChannelTable.Keys) {
    if ($lowerName -match $ChannelTable[$channel].Pattern) {
      return $channel
    }
  }

  return "unknown"
}

function Get-ChannelSortOrder {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Channel
  )

  if ($ChannelTable.Contains($Channel)) {
    return $ChannelTable[$Channel].SortOrder
  }

  return $UnknownChannelSortOrder
}

function Get-DownloadGuidance {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Channel
  )

  if ($ChannelTable.Contains($Channel)) {
    return $ChannelTable[$Channel].Guidance
  }

  return $UnknownChannelGuidance
}

function Get-AssetPurpose {
  param(
    [Parameter(Mandatory = $true)]
    [string]$AssetName
  )

  if ($AssetName -ieq $UniversalInstallerName) {
    return "Recommended installer. Detects your CPU and installs the matching build automatically."
  }

  $channel = Get-ChannelFromAssetName $AssetName

  if ($AssetName -match "-Setup\.exe$") {
    return "Manual installer for the $channel channel."
  }
  if ($AssetName -match "^releases\..*\.json$") {
    return "Velopack update feed for the $channel channel. UpdateChecker reads this file."
  }
  if ($AssetName -match "-full\.nupkg$") {
    return "Velopack full package for the $channel channel. Normal manual installs should use the setup executable instead."
  }
  if ($AssetName -match "-delta\.nupkg$") {
    return "Velopack delta package for the $channel channel. Update clients use it to reduce download size."
  }
  if ($AssetName -ieq "SHA256SUMS.txt") {
    return "SHA-256 checksums for the setup executables. The auto-detect installer verifies its download against this file."
  }
  if ($AssetName -match "^EqualizerAPO-XT-source-.*\.zip$") {
    return "Source snapshot for this exact release commit."
  }

  return "Release asset generated by the build pipeline."
}

function Format-FileSize {
  param(
    [Parameter(Mandatory = $true)]
    [long]$Bytes
  )

  if ($Bytes -ge 1GB) {
    return "{0:N1} GB" -f ($Bytes / 1GB)
  }
  if ($Bytes -ge 1MB) {
    return "{0:N1} MB" -f ($Bytes / 1MB)
  }
  if ($Bytes -ge 1KB) {
    return "{0:N1} KB" -f ($Bytes / 1KB)
  }

  return "$Bytes bytes"
}

function Escape-MarkdownCell {
  param(
    [AllowNull()]
    [string]$Text
  )

  if ($null -eq $Text) {
    return ""
  }

  return $Text.Replace("|", "\|").Replace("`r", " ").Replace("`n", " ")
}

function Escape-MarkdownText {
  param(
    [AllowNull()]
    [string]$Text
  )

  if ($null -eq $Text) {
    return ""
  }

  return $Text.Replace("[", "\[").Replace("]", "\]").Replace("`r", " ").Replace("`n", " ")
}

$release = Invoke-GhJson -Arguments @("api", "repos/$Repository/releases/tags/$Tag")
$assets = @($release.assets | Sort-Object name)
$installerAssets = @(
  $assets |
    Where-Object { $_.name -match "-Setup\.exe$" } |
    Sort-Object @{ Expression = {
        if ($_.name -ieq $UniversalInstallerName) {
          $UniversalInstallerSortOrder
        } else {
          Get-ChannelSortOrder (Get-ChannelFromAssetName $_.name)
        }
      } }, name
)
$sourceAssets = @($assets | Where-Object { $_.name -match "^EqualizerAPO-XT-source-.*\.zip$" })

if ($installerAssets.Count -eq 0) {
  throw "No setup executable assets were found for release $Tag"
}

if ($sourceAssets.Count -eq 0) {
  throw "No source zip asset was found for release $Tag"
}

$previousRelease = $null
$compare = $null

try {
  $recentReleases = @(Invoke-GhJson -Arguments @("api", "repos/$Repository/releases?per_page=20"))
  $previousRelease = $recentReleases |
    Where-Object { $_.tag_name -ne $Tag -and -not $_.draft } |
    Select-Object -First 1
} catch {
  Write-Warning "Could not read previous releases: $($_.Exception.Message)"
}

if ($previousRelease) {
  try {
    $compare = Invoke-GhJson -Arguments @("api", "repos/$Repository/compare/$($previousRelease.tag_name)...$TargetCommit")
  } catch {
    Write-Warning "Could not read compare data from $($previousRelease.tag_name) to $TargetCommit`: $($_.Exception.Message)"
  }
}

$shortCommit = if ($TargetCommit.Length -gt 7) { $TargetCommit.Substring(0, 7) } else { $TargetCommit }
$workflowUrl = "https://github.com/$Repository/actions/runs/$WorkflowRunId"
$releaseUrl = "https://github.com/$Repository/releases/tag/$Tag"

$lines = [System.Collections.Generic.List[string]]::new()
[void]$lines.Add("# EqualizerAPO-XT $PackVersion")
[void]$lines.Add("")
[void]$lines.Add("Built from [$shortCommit](https://github.com/$Repository/commit/$TargetCommit) by [GitHub Actions run $WorkflowRunId]($workflowUrl).")
[void]$lines.Add("")
[void]$lines.Add("## What to download")
[void]$lines.Add("")
[void]$lines.Add("| Download | Use this when |")
[void]$lines.Add("| --- | --- |")

foreach ($asset in $installerAssets) {
  if ($asset.name -ieq $UniversalInstallerName) {
    $guidance = $UniversalInstallerGuidance
  } else {
    $guidance = Get-DownloadGuidance (Get-ChannelFromAssetName $asset.name)
  }
  $name = Escape-MarkdownCell $asset.name
  [void]$lines.Add("| [$name]($($asset.browser_download_url)) | $(Escape-MarkdownCell $guidance) |")
}

[void]$lines.Add("")
[void]$lines.Add('The recommended download auto-detects your CPU and installs the matching build. The per-channel setup executables are for picking a specific build by hand. The `.nupkg` and `releases.*.json` files are for the Velopack/update pipeline.')
[void]$lines.Add("")
[void]$lines.Add("## All files")
[void]$lines.Add("")
[void]$lines.Add("| File | Size | Purpose |")
[void]$lines.Add("| --- | ---: | --- |")

foreach ($asset in $assets) {
  $name = Escape-MarkdownCell $asset.name
  $size = Format-FileSize ([long]$asset.size)
  $purpose = Escape-MarkdownCell (Get-AssetPurpose $asset.name)
  [void]$lines.Add("| [$name]($($asset.browser_download_url)) | $size | $purpose |")
}

[void]$lines.Add("")
[void]$lines.Add("## Changes")
[void]$lines.Add("")

if ($previousRelease -and $compare) {
  $previousTag = Escape-MarkdownText $previousRelease.tag_name
  $previousUrl = $previousRelease.html_url
  $compareUrl = if ($compare.html_url) { $compare.html_url } else { "https://github.com/$Repository/compare/$($previousRelease.tag_name)...$Tag" }
  [void]$lines.Add("Changes since [$previousTag]($previousUrl): [$($previousRelease.tag_name)...$Tag]($compareUrl)")
  [void]$lines.Add("")

  $commits = @($compare.commits)
  if ($commits.Count -eq 0) {
    [void]$lines.Add("- No commits were reported by the GitHub compare API.")
  } else {
    $maxCommits = 30
    $listedCommits = @($commits | Select-Object -Last $maxCommits)
    foreach ($commit in $listedCommits) {
      $commitSha = [string]$commit.sha
      $commitShort = if ($commitSha.Length -gt 7) { $commitSha.Substring(0, 7) } else { $commitSha }
      $message = Escape-MarkdownText (([string]$commit.commit.message -split "`n")[0])
      [void]$lines.Add("- [$commitShort]($($commit.html_url)) $message")
    }

    if ($commits.Count -gt $maxCommits) {
      $remaining = $commits.Count - $maxCommits
      [void]$lines.Add("- ...and $remaining more commit(s). Use the compare link above for the full list.")
    }
  }
} elseif ($previousRelease) {
  [void]$lines.Add("Previous release: [$($previousRelease.tag_name)]($($previousRelease.html_url)).")
  [void]$lines.Add("")
  [void]$lines.Add("The GitHub compare API was not available while generating these notes. Read the current commit here: [${shortCommit}](https://github.com/$Repository/commit/$TargetCommit).")
} else {
  [void]$lines.Add("No previous GitHub Release was found. Read the current commit here: [${shortCommit}](https://github.com/$Repository/commit/$TargetCommit).")
}

[void]$lines.Add("")
[void]$lines.Add("## Verification")
[void]$lines.Add("")

# The variant list comes from simd-variants.psd1 so this sentence cannot drift
# from what CI actually builds; the test-suite list mirrors the build.yml steps.
$builtVariantsPhrase = if ($manifestChannels.Count -gt 0) {
  $channelNames = @($manifestChannels | ForEach-Object { "``$_``" })
  $channelList = if ($channelNames.Count -gt 1) {
    (($channelNames | Select-Object -SkipLast 1) -join ", ") + ", and " + $channelNames[-1]
  } else {
    $channelNames[0]
  }
  "installers for the $channelList channels"
} else {
  "installers for every SIMD/architecture channel"
}
[void]$lines.Add("The linked workflow builds $builtVariantsPhrase. It runs EditorLogicTests on every build variant, and runs HybridConvTests, EngineOrchestrationTests, and AudioRegressionTests (plus a cross-variant output comparison) where the GitHub-hosted runner can execute the target instruction set.")
[void]$lines.Add("")
[void]$lines.Add("Release page: [$Tag]($releaseUrl)")

$outputDirectory = Split-Path -Path $OutputPath -Parent
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
  New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

Set-Content -Path $OutputPath -Encoding UTF8 -Value $lines
Write-Host "Wrote release notes to $OutputPath"
