<#
.SYNOPSIS
    Prepares a checkout for building: every dependency, Qt, and the
    environment the build scripts read. The one entry point for CI jobs, the
    scheduled audit, CodeQL and a developer's machine.

.DESCRIPTION
    The functions in Provisioning.psm1 were shared, but the order in which
    they are called was written five times (the build job, memcheck, the
    panel-input gate's asan leg, vst3-preview-probe.yml and setup-build.ps1)
    and the copies differed: one skipped the ASIO SDK, one never exported the
    dependency environment, only one exported Qt6_DIR, and three spelled the
    primary variant as "x64"/"avx2" (TECH_DEBT_AUDIT TD-35, audit #467). A
    checkout prepared by setup-build.ps1 could not run Build-Solution.ps1
    until QT_ROOT was set by hand, and the scheduled audit could not build
    the Qt applications.

    Steps, in order:
      1. Download the pinned binary dependencies and the ASIO SDK.
      2. For a variant the manifest marks UsesVcpkg, build FFTW and
         libsndfile with vcpkg and rebuild muparserx.
      3. Clone the pinned header-only source dependencies.
      4. Install Qt and check that its qmake runs and reports the pinned
         version (Install-QtSdk does both), unless -SkipQt.
      5. Export the dependency paths (Initialize-DepsEnvironment.ps1), QT_ROOT
         and Qt6_DIR, and put Qt's bin folder on PATH. Each goes to this
         process and, under GitHub Actions, to GITHUB_ENV / GITHUB_PATH, so
         later steps and the caller's own session find qmake by name.

    -PlanOnly returns the resolved variant and the ordered step names, for
    Pester.

.PARAMETER Simd
    SIMD variant. Empty means the manifest's Primary variant on x64; ARM64
    has one variant and ignores it.

.PARAMETER ReuseCachedDownloads
    Keep and reuse archives already in deps\_downloads (a developer's
    machine). CI downloads fresh.
#>
[CmdletBinding()]
param(
    [string] $WorkspaceRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [ValidateSet("x64", "ARM64")] [string] $Platform = "x64",
    [AllowEmptyString()] [string] $Simd = "",
    [AllowEmptyString()] [string] $QtVersion = "",
    [switch] $SkipQt,
    [switch] $ReuseCachedDownloads,
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "Provisioning.psm1") -Force
$manifest = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "..\simd-variants.psd1")
$variant = Resolve-BuildVariant -Manifest $manifest -Platform $Platform -Simd $Simd
if (-not $QtVersion) { $QtVersion = $manifest.Shared.QtVersion }

$depsRoot = Join-Path $WorkspaceRoot "deps"
$downloadRoot = Join-Path $depsRoot "_downloads"
$qtRoot = Join-Path $WorkspaceRoot "Qt\$QtVersion\$($variant.QtArchDir)"

$steps = @("DownloadDependencies")
if ($variant.UsesVcpkg) { $steps += "BuildVcpkgDependencies" }
$steps += "ClonePinnedSources"
if (-not $SkipQt) { $steps += "InstallQt" }
$steps += "ExportEnvironment"

$plan = [pscustomobject]@{
    Platform = $variant.Platform
    Simd     = $variant.Simd
    Variant  = $variant
    DepsRoot = $depsRoot
    QtRoot   = $qtRoot
    Steps    = $steps
}
if ($PlanOnly) { return $plan }

function Export-EnvironmentValue([string] $Name, [string] $Value) {
    if ($env:GITHUB_ENV) { "$Name=$Value" >> $env:GITHUB_ENV }
    Set-Item -Path "env:$Name" -Value $Value
    Write-Host "$Name=$Value"
}

Write-Host "=== Workspace: $WorkspaceRoot ($($variant.Platform)/$($variant.Simd)) ==="

Write-Host "`n=== Download dependencies ==="
$downloads = Get-DependencyDownloadSpec -Manifest $manifest -DepsRoot $depsRoot `
    -Platform $variant.Platform -Simd $variant.Simd
Invoke-DependencyDownload -Downloads $downloads -DownloadRoot $downloadRoot `
    -ReuseCachedDownloads:$ReuseCachedDownloads
# The Steinberg ASIO SDK (source zip, GPLv3 dual-licensed), same pin for
# every variant; extracts to deps\asiosdk\ASIOSDK.
Invoke-DependencyDownload -Downloads (Get-SdkDownloadSpec -Manifest $manifest -DepsRoot $depsRoot) `
    -DownloadRoot $downloadRoot -ReuseCachedDownloads:$ReuseCachedDownloads

if ($variant.UsesVcpkg) {
    Write-Host "`n=== Build lower-SIMD dependencies with vcpkg ==="
    . (Join-Path $PSScriptRoot "Import-VsDevEnvironment.ps1")
    Import-VsDevEnvironment $variant.MsvcDevPlatform
    Build-VcpkgDependencies -DepsRoot $depsRoot `
        -VcpkgFallbackRoot (Join-Path $WorkspaceRoot "vcpkg") `
        -SimdVariant $variant.Simd `
        -VcpkgCommit $manifest.Shared.VcpkgCommit
}

Write-Host "`n=== Clone pinned source dependencies ==="
Install-PinnedSourceDependency -Manifest $manifest -DepsRoot $depsRoot

if (-not $SkipQt) {
    Write-Host "`n=== Install Qt $QtVersion ==="
    $installedRoot = Install-QtSdk -WorkspaceRoot $WorkspaceRoot -Platform $variant.Platform -QtVersion $QtVersion
    if ([System.IO.Path]::GetFullPath($installedRoot) -ne [System.IO.Path]::GetFullPath($qtRoot)) {
        throw "Qt was installed at $installedRoot, not at the planned $qtRoot"
    }
}

Write-Host "`n=== Export the build environment ==="
& (Join-Path $PSScriptRoot "Initialize-DepsEnvironment.ps1") -DepsPath $depsRoot
# A checkout prepared with -SkipQt may still hold Qt from an earlier run;
# export it when it is there so the build scripts need no hand-set QT_ROOT.
if (Test-Path (Join-Path $qtRoot "bin\qmake.exe")) {
    Export-EnvironmentValue "QT_ROOT" $qtRoot
    Export-EnvironmentValue "Qt6_DIR" $qtRoot
    $qtBin = Join-Path $qtRoot "bin"
    if ($env:GITHUB_PATH) { $qtBin >> $env:GITHUB_PATH }
    if (($env:PATH -split ";") -notcontains $qtBin) { $env:PATH = "$qtBin;$env:PATH" }
    Write-Host "PATH += $qtBin"
}
elseif (-not $SkipQt) {
    throw "qmake.exe is missing under $qtRoot after the Qt install"
}

Write-Host "`nWorkspace ready."
# A `shell: pwsh` step ends with `exit $LASTEXITCODE`; every native call above
# was checked where it ran, so a stale non-zero code must not fail the step.
$global:LASTEXITCODE = 0
