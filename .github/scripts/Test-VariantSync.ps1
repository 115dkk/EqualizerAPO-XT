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
