<#
.SYNOPSIS
    Sets up the build environment for EqualizerAPO-XT.
    Downloads external dependencies and installs Qt 6.

.DESCRIPTION
    This script:
    1. Downloads FFTW, muparserx, libsndfile from their GitHub release pages
    2. Clones the TCLAP header-only library and the VST3 SDK pluginterfaces
    3. Installs Qt 6.10.1 via aqtinstall (Python)

    All sources match the project's CI workflow (.github/workflows/build.yml).

.PARAMETER SkipQt
    Skip Qt installation (useful if Qt is already installed).

.PARAMETER QtVersion
    Qt version to install. Default: 6.10.1

.PARAMETER SimdVariant
    SIMD variant for dependencies. Default: avx2
    Options: sse2, avx, avx2, avx512, avx10_1

.PARAMETER Platform
    Target platform. Default: x64
    Options: x64, ARM64
#>
param(
    [switch]$SkipQt,
    # Audit #250 F059: empty means "use Shared.QtVersion from the manifest".
    [string]$QtVersion = "",
    [ValidateSet("sse2", "avx", "avx2", "avx512", "avx10_1")]
    [string]$SimdVariant = "avx2",
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$workspace = $PSScriptRoot

# Single source of truth for the SIMD variant set and the pinned dependency
# tags/versions, shared with build.yml and New-ReleaseNotes.ps1. Import-PowerShellDataFile
# is the restricted-language loader, so this stays plain data.
$manifestPath = Join-Path $workspace ".github\simd-variants.psd1"
$simdManifest = Import-PowerShellDataFile -Path $manifestPath
if (-not $QtVersion) { $QtVersion = $simdManifest.Shared.QtVersion }

# Shared provisioning implementation (download+verify, vcpkg build, Qt install),
# consumed by both this script and .github/workflows/build.yml so the two can
# no longer drift (audit finding TD015).
Import-Module (Join-Path $workspace ".github\scripts\Provisioning.psm1") -Force

Write-Host "=== EqualizerAPO-XT Build Setup ===" -ForegroundColor Cyan
Write-Host "Workspace: $workspace"
Write-Host "Platform:  $Platform"
Write-Host "SIMD:      $SimdVariant"
Write-Host ""

# --- Dependency asset mapping (driven by .github/simd-variants.psd1) ---
$variant = if ($Platform -eq "ARM64") { "neon" } else { $SimdVariant }
$variantEntry = Get-SimdVariantEntry -Manifest $simdManifest -Platform $Platform -Simd $variant
$usesVcpkg = [bool]$variantEntry.UsesVcpkg

# --- 1. Download binary dependencies ---
Write-Host "`n=== Step 1: Download Dependencies ===" -ForegroundColor Yellow

$depsDir = Join-Path $workspace "deps"
$downloadDir = Join-Path $depsDir "_downloads"

# muparserx and velopack_libc always; FFTW and libsndfile only for the variants
# that do not build them from vcpkg. URLs and SHA-256 pins come from the
# manifest; a cached zip in deps\_downloads is reused, and a mismatching one is
# deleted so the next run redownloads it.
$downloads = Get-DependencyDownloadSpec -Manifest $simdManifest -DepsRoot $depsDir -Platform $Platform -Simd $variant
Invoke-DependencyDownload -Downloads $downloads -DownloadRoot $downloadDir -ReuseCachedDownloads

# For SSE2/AVX variants, build FFTW and libsndfile from vcpkg
if ($usesVcpkg) {
    Write-Host "`n  Building lower-SIMD dependencies via vcpkg..."

    # Import VS dev environment (canonical helper shared with build.yml)
    . (Join-Path $workspace ".github\scripts\Import-VsDevEnvironment.ps1")
    Import-VsDevEnvironment "x64"

    Build-VcpkgDependencies -DepsRoot $depsDir `
        -VcpkgFallbackRoot (Join-Path $workspace "vcpkg") `
        -SimdVariant $SimdVariant `
        -VcpkgCommit $simdManifest.Shared.VcpkgCommit
}

Write-Host "  Dependencies downloaded." -ForegroundColor Green

# --- 2. Clone TCLAP ---
# Pinned to the manifest's TclapTag, matching the build.yml checkout. Cached
# clones are re-checked against the tag so a TclapTag bump actually takes
# effect (the zip downloads above get the same treatment via SHA-256).
# Audit #250 F072/F058: one checkout routine for the three source
# dependencies. All of them re-verify a cached checkout against the pinned
# tag (previously only TCLAP did, so a tag bump left local trees stale), and
# all of them assert the reviewed commit SHA from the manifest - a git tag is
# movable, and a silently retargeted tag must fail here, not ship.
function Sync-PinnedCheckout {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [string]$RepoUrl,
        [Parameter(Mandatory)] [string]$Tag,
        [Parameter(Mandatory)] [string]$ExpectedCommit,
        [Parameter(Mandatory)] [string]$CheckoutDir,
        [Parameter(Mandatory)] [string]$ProbeFile
    )

    $cachedCommit = $null
    if ((Test-Path (Join-Path $CheckoutDir $ProbeFile)) -and (Test-Path (Join-Path $CheckoutDir ".git"))) {
        $cachedCommit = (git -C $CheckoutDir rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -ne 0) { $cachedCommit = $null; $global:LASTEXITCODE = 0 }
    }
    if ($cachedCommit -eq $ExpectedCommit -and $cachedCommit) {
        Write-Host "  [cached] $Name already present at $Tag ($ExpectedCommit)"
        return
    }

    if (Test-Path $CheckoutDir) {
        Write-Host "  Cached $Name is not at $Tag/$ExpectedCommit; re-cloning..."
        Remove-Item $CheckoutDir -Recurse -Force
    }
    git clone --depth 1 --branch $Tag $RepoUrl $CheckoutDir
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone $Name" }

    $actualCommit = (git -C $CheckoutDir rev-parse HEAD)
    if ($actualCommit -ne $ExpectedCommit) {
        throw "$Name tag $Tag resolved to $actualCommit, but the manifest pins $ExpectedCommit. The tag moved - review the diff and update simd-variants.psd1 deliberately."
    }
    Write-Host "  -> $CheckoutDir ($actualCommit)"
}

Write-Host "`n=== Step 2: Clone TCLAP ===" -ForegroundColor Yellow
Sync-PinnedCheckout -Name "TCLAP" -RepoUrl "https://github.com/115dkk/tclap" `
    -Tag $simdManifest.Shared.TclapTag -ExpectedCommit $simdManifest.Shared.TclapCommit `
    -CheckoutDir (Join-Path $depsDir "tclap") -ProbeFile "include"

# --- 2b. Clone VST3 SDK (pluginterfaces only) ---
# VST3 hosting only needs the Steinberg pluginterfaces headers (COM-style interface
# definitions + the IID instantiation unit). public.sdk / vstgui / samples are not
# compiled, so we fetch just the pluginterfaces submodule. Pinned to the 3.8.0 tag,
# which is the first MIT-licensed release (compatible with our GPLv2-or-later code).
Write-Host "`n=== Step 2b: Clone VST3 SDK (pluginterfaces) ===" -ForegroundColor Yellow
$vst3Dir = Join-Path $depsDir "vst3sdk"
New-Item -ItemType Directory -Force -Path $vst3Dir | Out-Null
Sync-PinnedCheckout -Name "VST3 pluginterfaces" -RepoUrl "https://github.com/steinbergmedia/vst3_pluginterfaces" `
    -Tag $simdManifest.Shared.Vst3Tag -ExpectedCommit $simdManifest.Shared.Vst3Commit `
    -CheckoutDir (Join-Path $vst3Dir "pluginterfaces") -ProbeFile "base\funknown.h"

# --- 2c. Clone Google Highway (header-only portable SIMD) ---
# The Common DSP kernels use Highway in static per-target dispatch mode, so only
# the headers are needed (no libhwy build, no runtime dispatch table).
Write-Host "`n=== Step 2c: Clone Highway ===" -ForegroundColor Yellow
Sync-PinnedCheckout -Name "Highway" -RepoUrl "https://github.com/google/highway" `
    -Tag $simdManifest.Shared.HighwayTag -ExpectedCommit $simdManifest.Shared.HighwayCommit `
    -CheckoutDir (Join-Path $depsDir "highway") -ProbeFile "hwy\highway.h"

# --- 3. Install Qt ---
# $qtArchDir is also needed by the verification section below when Qt was
# installed on an earlier run and this one passes -SkipQt.
$qtArchDir = if ($Platform -eq "ARM64") { "msvc2022_arm64" } else { "msvc2022_64" }
if ($SkipQt) {
    Write-Host "`n=== Step 3: Qt Installation (SKIPPED) ===" -ForegroundColor Yellow
} else {
    Write-Host "`n=== Step 3: Install Qt $QtVersion ===" -ForegroundColor Yellow

    $qtRoot = Install-QtSdk -WorkspaceRoot $workspace -Platform $Platform -QtVersion $QtVersion

    Write-Host "  Qt installed at: $qtRoot" -ForegroundColor Green
}

# --- 4. Verify ---
Write-Host "`n=== Verification ===" -ForegroundColor Yellow

$checks = @(
    @{ Name = "FFTW header";     Path = "$depsDir\fftw\include\fftw3.h" },
    @{ Name = "FFTW lib";        Path = "$depsDir\fftw\Release\libfftw3.lib" },
    @{ Name = "muparserx lib";   Path = "$depsDir\muparserx\build\Release\muparserx.lib" },
    @{ Name = "muparserx header"; Path = "$depsDir\muparserx\parser\mpParser.h" },
    @{ Name = "libsndfile header"; Path = "$depsDir\libsndfile\include\sndfile.h" },
    @{ Name = "libsndfile lib";  Path = "$depsDir\libsndfile\build\Release\sndfile.lib" },
    @{ Name = "TCLAP header";    Path = "$depsDir\tclap\include\tclap\CmdLine.h" },
    @{ Name = "VST3 pluginterfaces"; Path = "$depsDir\vst3sdk\pluginterfaces\base\funknown.h" },
    @{ Name = "Highway header";  Path = "$depsDir\highway\hwy\highway.h" }
)

# velopack_libc ships per-arch import libs/DLLs in a single zip; check the one we link.
$velopackArch = if ($Platform -eq "ARM64") { "arm64" } else { "x64" }
$checks += @(
    @{ Name = "Velopack header"; Path = "$depsDir\velopack_libc\include\Velopack.hpp" },
    @{ Name = "Velopack import lib"; Path = "$depsDir\velopack_libc\lib\velopack_libc_win_${velopackArch}_msvc.dll.lib" },
    @{ Name = "Velopack DLL";    Path = "$depsDir\velopack_libc\lib\velopack_libc_win_${velopackArch}_msvc.dll" }
)

$allOk = $true
foreach ($check in $checks) {
    if (Test-Path $check.Path) {
        Write-Host "  [OK] $($check.Name)" -ForegroundColor Green
    } else {
        Write-Host "  [MISSING] $($check.Name): $($check.Path)" -ForegroundColor Red
        $allOk = $false
    }
}

if (-not $SkipQt) {
    $qtRoot = Join-Path $workspace "Qt\$QtVersion\$qtArchDir"
    $qtChecks = @(
        @{ Name = "qmake";    Path = "$qtRoot\bin\qmake.exe" },
        @{ Name = "Qt6Core";  Path = "$qtRoot\lib\Qt6Core.lib" },
        @{ Name = "lrelease"; Path = "$qtRoot\bin\lrelease.exe" }
    )
    foreach ($check in $qtChecks) {
        if (Test-Path $check.Path) {
            Write-Host "  [OK] $($check.Name)" -ForegroundColor Green
        } else {
            Write-Host "  [MISSING] $($check.Name): $($check.Path)" -ForegroundColor Red
            $allOk = $false
        }
    }
}

if ($allOk) {
    Write-Host "`n=== Setup Complete ===" -ForegroundColor Green

    # The .pro files fail with error() when an x64 build passes neither
    # EAPO_SIMD_FLAGS nor EAPO_SIMD_BASELINE, so the printed qmake line must
    # carry the same variant arguments CI passes (build.yml "Build Qt
    # Applications"): the update channel plus the variant's /arch flag, or the
    # explicit baseline marker for sse2. ARM64 passes no SIMD argument.
    $qmakeVariantArgs = "`"EAPO_UPDATE_CHANNEL=$($variantEntry.Channel)`""
    if ($Platform -eq "x64") {
        if ($variantEntry.QtArchFlag) {
            $qmakeVariantArgs += " `"EAPO_SIMD_FLAGS=$($variantEntry.QtArchFlag)`""
        } else {
            $qmakeVariantArgs += " `"EAPO_SIMD_BASELINE=1`""
        }
    }

    Write-Host @"

To build the project, open a VS Developer Command Prompt and run:

  # Set environment variables
  `$env:FFTW_INCLUDE     = "$depsDir\fftw\include"
  `$env:FFTW_LIB         = "$depsDir\fftw\Release"
  `$env:MUPARSERX_INCLUDE = "$depsDir\muparserx\parser"
  `$env:MUPARSERX_LIB    = "$depsDir\muparserx\build\Release"
  `$env:LIBSNDFILE_INCLUDE = "$depsDir\libsndfile\include"
  `$env:LIBSNDFILE_LIB   = "$depsDir\libsndfile\build\Release"
  `$env:VELOPACK_INCLUDE = "$depsDir\velopack_libc\include"
  `$env:VELOPACK_LIB     = "$depsDir\velopack_libc\lib"
  `$env:TCLAP_ROOT       = "$depsDir\tclap"
  `$env:HIGHWAY_INCLUDE  = "$depsDir\highway"
  `$env:QT_ROOT          = "$qtRoot"

  # Build core projects with MSBuild
  msbuild EqualizerAPO.sln /p:Configuration=Release /p:Platform=$Platform

  # Build Qt apps with qmake (run lrelease first: nmake stalls without the .qm files)
  mkdir build-Editor-$Platform; cd build-Editor-$Platform
  lrelease ..\Editor\Editor.pro
  qmake ..\Editor\Editor.pro -r "CONFIG+=release" $qmakeVariantArgs
  nmake

  # DeviceSelector and UpdateChecker build the same way from their .pro files.
"@
} else {
    Write-Host "`n=== Setup Incomplete ===" -ForegroundColor Red
    Write-Host "Some dependencies are missing. Check the output above."
    exit 1
}
