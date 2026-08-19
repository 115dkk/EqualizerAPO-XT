<#
.SYNOPSIS
    Fails when the channel strings compiled into Installer/AutoInstallerLogic.cpp drift
    from .github/simd-variants.psd1.

.DESCRIPTION
    AutoInstallerLogic.cpp cannot read the manifest (it is a dependency-free compiled
    C++ binary), so detectChannel() returns hardcoded channel literals. This lint
    extracts every channel-shaped wide-string literal (L"x64-..." / L"arm64-...")
    from the installer source and requires set equality with the manifest's
    Variants[].Channel, turning the file's "MUST stay in sync" comment into a
    build failure instead of a hope.
#>
param(
  [string]$RepoRoot = (Join-Path $PSScriptRoot ".." "..")
)

$ErrorActionPreference = "Stop"

$manifestPath = Join-Path $RepoRoot ".github" "simd-variants.psd1"
$manifest = Import-PowerShellDataFile -Path $manifestPath
$manifestChannels = @($manifest.Variants | ForEach-Object { $_.Channel } | Sort-Object -Unique)

$installerPath = Join-Path $RepoRoot "Installer" "AutoInstallerLogic.cpp"
$source = Get-Content -Path $installerPath -Raw
$pattern = 'L"((?:x64|arm64)-[a-z0-9][a-z0-9-]*)"'
$installerChannels = @(
  [regex]::Matches($source, $pattern) | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
)

$missingInInstaller = @($manifestChannels | Where-Object { $installerChannels -notcontains $_ })
$unknownInInstaller = @($installerChannels | Where-Object { $manifestChannels -notcontains $_ })

if ($missingInInstaller.Count -gt 0 -or $unknownInInstaller.Count -gt 0) {
  if ($missingInInstaller.Count -gt 0) {
    Write-Host "::error file=Installer/AutoInstallerLogic.cpp::Missing manifest channels: $($missingInInstaller -join ', ')"
  }
  if ($unknownInInstaller.Count -gt 0) {
    Write-Host "::error file=Installer/AutoInstallerLogic.cpp::Channels not in simd-variants.psd1: $($unknownInInstaller -join ', ')"
  }
  throw "Installer/AutoInstallerLogic.cpp and .github/simd-variants.psd1 are out of sync."
}

Write-Host "AutoInstallerLogic.cpp channels match simd-variants.psd1: $($manifestChannels -join ', ')"

# Audit #275 D2/TD-21: the /arch decision has exactly one mechanism - the
# EapoVariantArch opt-in resolved by Directory.Build.props (locally AVX2,
# overridden per CI leg via /p:EnableEnhancedInstructionSet). A literal
# EnableEnhancedInstructionSet in a project file is a second mechanism that
# silently beats both, which is how a Debug|x64 Benchmark once ignored every
# /p: override. Fail on any such literal.
$projectFiles = @(Get-ChildItem -Path $RepoRoot -Recurse -Filter *.vcxproj -File |
  Where-Object { $_.FullName -notmatch '[\\/](deps|build-[^\\/]+|\.claude|\.codex-worktrees)[\\/]' })
$offenders = @()
foreach ($project in $projectFiles) {
  $content = Get-Content -Path $project.FullName -Raw
  if ($content -match '<EnableEnhancedInstructionSet>') {
    $offenders += $project.FullName.Substring($RepoRoot.Length).TrimStart('\', '/')
  }
}
if ($offenders.Count -gt 0) {
  foreach ($offender in $offenders) {
    Write-Host "::error file=$offender::Literal <EnableEnhancedInstructionSet> found; use the EapoVariantArch opt-in in Directory.Build.props instead."
  }
  throw "Per-project EnableEnhancedInstructionSet literals bypass the single /arch mechanism."
}

Write-Host "No per-project EnableEnhancedInstructionSet literals outside Directory.Build.props."
