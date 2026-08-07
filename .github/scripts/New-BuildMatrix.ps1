<#
.SYNOPSIS
    Expands .github/simd-variants.psd1 into the build job matrix for
    .github/workflows/build.yml, so a variant is authored in exactly one place.

.DESCRIPTION
    Writes a GitHub Actions output named `matrix` containing
    { name: [...], include: [...] }. Pull requests build only the Primary
    variant; pushes and manual dispatches build the full set.

    Also writes an output named `runner_executable_variants`: a JSON array of
    the x64 Simd names whose binaries GitHub-hosted runners can execute at
    runtime (RunnerCanExecute). The cross-variant-compare job hands it to
    Tests/AudioRegressionTests/scripts/cross_variant_compare.py so the audio
    gating list is derived from the manifest instead of being hard-coded.

    Runs on the ubuntu prepare-matrix job (pwsh), and locally for inspection:
      pwsh .github/scripts/New-BuildMatrix.ps1 -EventName push
#>
param(
  [Parameter(Mandatory = $true)]
  [string]$EventName
)

$ErrorActionPreference = "Stop"

$manifestPath = Join-Path $PSScriptRoot ".." "simd-variants.psd1"
$manifest = Import-PowerShellDataFile -Path $manifestPath

$include = @()
foreach ($variant in $manifest.Variants) {
  $isArm = $variant.Platform -eq 'ARM64'

  # Keys mirror what build.yml's hand-written matrix.include block used to define.
  # windows-2025-vs2026 ships VS 2026 (v145); windows-11-arm still ships VS 2022
  # (v143) — the build step picks the platform toolset from matrix.platform.
  # The per-variant asset zip names are NOT expanded into the matrix: the
  # download step resolves them from the manifest by platform+simd via
  # .github/scripts/Provisioning.psm1 (Get-DependencyDownloadSpec).
  $entry = [ordered]@{
    name            = $variant.Name
    os              = if ($isArm) { 'windows-11-arm' } else { 'windows-2025-vs2026' }
    platform        = $variant.Platform
    simd_variant    = $variant.Simd
    msvcdevplatform = if ($isArm) { 'arm64' } else { 'x64' }
    uses_vcpkg      = [bool]$variant.UsesVcpkg
    # Audit #250 F066: the Velopack channel rides the matrix instead of being
    # re-derived from the simd name by string convention downstream.
    channel         = $variant.Channel
    can_execute     = [bool]$variant.RunnerCanExecute
    primary         = [bool]$variant.Primary
  }

  if (-not $isArm) {
    # sse2 carries no ArchFlag in the manifest but must explicitly pass NotSet so
    # a CI build never falls back to the project files' local AVX2 default.
    $entry.arch_flag = if ($variant.ArchFlag) { $variant.ArchFlag } else { 'NotSet' }
  }
  if ($variant.QtArchFlag) {
    $entry.qt_arch_flag = $variant.QtArchFlag
  }

  $include += [pscustomobject]$entry
}

if ($EventName -eq 'pull_request') {
  $include = @($include | Where-Object { $_.primary })
  if ($include.Count -eq 0) {
    throw "No Primary variant in simd-variants.psd1; pull requests would build nothing."
  }
}

$matrix = [ordered]@{
  name    = @($include | ForEach-Object { $_.name })
  include = @($include)
}

$json = $matrix | ConvertTo-Json -Compress -Depth 5
Write-Host "Build matrix ($EventName): $json"

# Cross-variant audio comparison gates only on the x64 variants the hosted
# runner is guaranteed to execute (RunnerCanExecute); ARM64 output is captured
# for inspection but not diffed. Derived from the full manifest (not the
# PR-filtered $include) because the comparison job never runs on pull requests.
$compareVariants = @($manifest.Variants |
  Where-Object { $_.Platform -eq 'x64' -and $_.RunnerCanExecute } |
  ForEach-Object { $_.Simd })
$compareJson = ConvertTo-Json -InputObject $compareVariants -Compress
Write-Host "Runner-executable x64 variants: $compareJson"

if ($env:GITHUB_OUTPUT) {
  "matrix=$json" >> $env:GITHUB_OUTPUT
  "runner_executable_variants=$compareJson" >> $env:GITHUB_OUTPUT
}
