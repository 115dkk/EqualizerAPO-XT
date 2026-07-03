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
    [string]$QtVersion = "6.10.1",
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

# velopack_libc ships as a single cross-platform zip attached to the velopack/velopack
# release. The version is pinned in the manifest so the asset name and download URL
# stay in sync with CI.
$velopackLibcVersion = $simdManifest.Shared.VelopackLibcVersion

# Supply-chain pins for the prebuilt binary dependencies: release tag plus SHA-256
# per asset, shared with build.yml. Downloads are refused on hash mismatch.
$dependencyPins = $simdManifest.DependencyReleases

Write-Host "=== EqualizerAPO-XT Build Setup ===" -ForegroundColor Cyan
Write-Host "Workspace: $workspace"
Write-Host "Platform:  $Platform"
Write-Host "SIMD:      $SimdVariant"
Write-Host ""

# --- Dependency asset mapping (driven by .github/simd-variants.psd1) ---
$variant = if ($Platform -eq "ARM64") { "neon" } else { $SimdVariant }
$variantEntry = $simdManifest.Variants | Where-Object { $_.Platform -eq $Platform -and $_.Simd -eq $variant } | Select-Object -First 1
if (-not $variantEntry) {
    throw "No asset mapping for Platform=$Platform, Variant=$variant"
}

# Reshape the manifest entry into the { muparserx; fftw; sndfile } hashtable the
# download loop below expects. Variants built from vcpkg leave fftw/sndfile $null,
# which keeps them out of $downloads exactly as before.
$assets = @{ muparserx = $variantEntry.Muparserx }
if ($variantEntry.Fftw)    { $assets.fftw = $variantEntry.Fftw }
if ($variantEntry.Sndfile) { $assets.sndfile = $variantEntry.Sndfile }

$usesVcpkg = $Platform -eq "x64" -and ($SimdVariant -eq "sse2" -or $SimdVariant -eq "avx")

# --- 1. Download binary dependencies ---
Write-Host "`n=== Step 1: Download Dependencies ===" -ForegroundColor Yellow

$depsDir = Join-Path $workspace "deps"
$downloadDir = Join-Path $depsDir "_downloads"
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null

$downloads = @(
    @{
        Repo        = "TheFireKahuna/muparserx"
        Asset       = $assets.muparserx
        Destination = Join-Path $depsDir "muparserx"
    },
    @{
        # Velopack C/C++ runtime (Velopack.h + import libs + DLLs for every platform).
        # Always fetched regardless of SIMD variant; the Editor links it for auto-update.
        # Not covered by DependencyReleases (the URL is release-tag based already), so
        # its SHA-256 pin rides on the download entry itself.
        Repo        = "velopack/velopack"
        Asset       = "velopack_libc_$velopackLibcVersion.zip"
        Url         = "https://github.com/velopack/velopack/releases/download/$velopackLibcVersion/velopack_libc_$velopackLibcVersion.zip"
        Sha256      = $simdManifest.Shared.VelopackLibcSha256
        Destination = Join-Path $depsDir "velopack_libc"
    }
)

if (-not $usesVcpkg) {
    if ($assets.fftw) {
        $downloads += @{
            Repo        = "TheFireKahuna/amd-fftw"
            Asset       = $assets.fftw
            Destination = Join-Path $depsDir "fftw"
        }
    }
    if ($assets.sndfile) {
        $downloads += @{
            Repo        = "TheFireKahuna/libsndfile"
            Asset       = $assets.sndfile
            Destination = Join-Path $depsDir "libsndfile"
        }
    }
}

foreach ($dl in $downloads) {
    $pin = $dependencyPins[$dl.Repo]
    $url = if ($dl.Url) {
        $dl.Url
    } elseif ($pin) {
        "https://github.com/$($dl.Repo)/releases/download/$($pin.Tag)/$($dl.Asset)"
    } else {
        throw "No pinned tag or explicit URL for $($dl.Repo) in simd-variants.psd1"
    }
    $zipPath = Join-Path $downloadDir $dl.Asset

    if (Test-Path $zipPath) {
        Write-Host "  [cached] $($dl.Asset)"
    } else {
        Write-Host "  Downloading $($dl.Asset) from $($dl.Repo)..."
        curl.exe --fail --location --retry 5 --retry-delay 5 --output $zipPath $url
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to download $url"
        }
    }

    if ($pin -or $dl.Sha256) {
        # DependencyReleases pins are per-asset; velopack_libc carries its pin on
        # the download entry (Shared.VelopackLibcSha256 in simd-variants.psd1).
        $expected = if ($dl.Sha256) { $dl.Sha256 } else { $pin.Sha256[$dl.Asset] }
        if (-not $expected) {
            throw "No pinned SHA-256 for $($dl.Asset) in simd-variants.psd1"
        }
        $actual = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash
        if ($actual -ne $expected) {
            Remove-Item $zipPath -Force
            throw "SHA-256 mismatch for $($dl.Asset): expected $expected, got $actual (stale cache removed; rerun to redownload)"
        }
        Write-Host "  Verified SHA-256 for $($dl.Asset)"
    }

    New-Item -ItemType Directory -Force -Path $dl.Destination | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $dl.Destination -Force
    Write-Host "  -> $($dl.Destination)"
}

# For SSE2/AVX variants, build FFTW and libsndfile from vcpkg
if ($usesVcpkg) {
    Write-Host "`n  Building lower-SIMD dependencies via vcpkg..."

    # Import VS dev environment (canonical helper shared with build.yml)
    . (Join-Path $workspace ".github\scripts\Import-VsDevEnvironment.ps1")
    Import-VsDevEnvironment "x64"

    $vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
    if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
        $vcpkgRoot = Join-Path $workspace "vcpkg"
        if (-not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
            # Pin vcpkg to the manifest commit: cloning a moving HEAD would let the
            # portfiles (and thus the FFTW/libsndfile binaries built here) change
            # without a reviewed diff in simd-variants.psd1. --depth 1 keeps the
            # clone small; the pinned commit is fetched by SHA and checked out.
            $vcpkgCommit = $simdManifest.Shared.VcpkgCommit
            git clone --depth 1 https://github.com/microsoft/vcpkg $vcpkgRoot
            if ($LASTEXITCODE -ne 0) { throw "Failed to clone vcpkg" }
            git -C $vcpkgRoot fetch --depth 1 origin $vcpkgCommit
            if ($LASTEXITCODE -ne 0) { throw "Failed to fetch pinned vcpkg commit $vcpkgCommit" }
            git -C $vcpkgRoot checkout --detach $vcpkgCommit
            if ($LASTEXITCODE -ne 0) { throw "Failed to check out pinned vcpkg commit $vcpkgCommit" }
            & (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
        }
    }

    $vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
    $fftwFeature = if ($SimdVariant -eq "avx") { "fftw3[avx,threads]:x64-windows" } else { "fftw3[sse2,threads]:x64-windows" }
    & $vcpkgExe install $fftwFeature "libsndfile:x64-windows" --clean-after-build
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }

    $installedRoot = Join-Path $vcpkgRoot "installed\x64-windows"

    # Copy FFTW
    $fftwInclude = Join-Path $depsDir "fftw\include"
    $fftwRelease = Join-Path $depsDir "fftw\Release"
    New-Item -ItemType Directory -Force -Path $fftwInclude, $fftwRelease | Out-Null
    Copy-Item (Join-Path $installedRoot "include\fftw3.h") $fftwInclude -Force

    $doubleFftwFilter = { $_.Name -notmatch "fftw3f|fftw3l|threads|omp" }
    $fftwLibFiles = @(Get-ChildItem (Join-Path $installedRoot "lib") -Recurse -Filter "*fftw3*" -File | Where-Object $doubleFftwFilter | Where-Object { $_.Extension -eq ".lib" })
    if ($fftwLibFiles.Count -gt 0) {
        Copy-Item $fftwLibFiles[0].FullName (Join-Path $fftwRelease "libfftw3.lib") -Force
    }
    $fftwDlls = @(Get-ChildItem (Join-Path $installedRoot "bin") -Filter "*fftw3*.dll" -File | Where-Object $doubleFftwFilter)
    $fftwDlls | Copy-Item -Destination $fftwRelease -Force
    if (-not (Test-Path (Join-Path $fftwRelease "libfftw3.dll")) -and $fftwDlls.Count -gt 0) {
        Copy-Item $fftwDlls[0].FullName (Join-Path $fftwRelease "libfftw3.dll") -Force
    }

    # Copy libsndfile
    $sndInclude = Join-Path $depsDir "libsndfile\include"
    $sndRelease = Join-Path $depsDir "libsndfile\build\Release"
    New-Item -ItemType Directory -Force -Path $sndInclude, $sndRelease | Out-Null
    Copy-Item (Join-Path $installedRoot "include\sndfile*.h*") $sndInclude -Force
    $sndLibs = Get-ChildItem (Join-Path $installedRoot "lib") -Filter "sndfile.lib" -File
    if ($sndLibs) { Copy-Item $sndLibs[0].FullName (Join-Path $sndRelease "sndfile.lib") -Force }
    Get-ChildItem (Join-Path $installedRoot "bin") -Filter "*.dll" -File | Copy-Item -Destination $sndRelease -Force

    # Rebuild muparserx with correct arch flags
    $parserDir = Join-Path $depsDir "muparserx\parser"
    $muparserBuildDir = Join-Path $depsDir "muparserx\build\Release"
    $muparserObjDir = Join-Path $depsDir "muparserx\build\obj-$SimdVariant"
    New-Item -ItemType Directory -Force -Path $muparserBuildDir, $muparserObjDir | Out-Null

    $archArg = if ($SimdVariant -eq "avx") { "/arch:AVX" } else { "" }
    $objects = @()
    $sources = Get-ChildItem $parserDir -Filter "*.cpp" -File | Where-Object { $_.Name -ne "mpTest.cpp" }
    foreach ($source in $sources) {
        $objectPath = Join-Path $muparserObjDir ($source.BaseName + ".obj")
        $clArgs = @("/nologo", "/c", "/EHsc", "/std:c++17", "/O2", "/MD", "/DNDEBUG", "/DMUP_USE_WIDE_STRING", "/I$parserDir", "/Fo$objectPath")
        if ($archArg) { $clArgs += $archArg }
        $clArgs += $source.FullName
        & cl.exe @clArgs
        if ($LASTEXITCODE -ne 0) { throw "muParserX compile failed for $($source.Name)" }
        $objects += $objectPath
    }
    & lib.exe /nologo "/OUT:$(Join-Path $muparserBuildDir 'muparserx.lib')" $objects
    if ($LASTEXITCODE -ne 0) { throw "muParserX lib creation failed" }
}

Write-Host "  Dependencies downloaded." -ForegroundColor Green

# --- 2. Clone TCLAP ---
# Pinned to the manifest's TclapTag, matching the build.yml checkout. Cached
# clones are re-checked against the tag so a TclapTag bump actually takes
# effect (the zip downloads above get the same treatment via SHA-256).
Write-Host "`n=== Step 2: Clone TCLAP ===" -ForegroundColor Yellow
$tclapTag = $simdManifest.Shared.TclapTag
$tclapDir = Join-Path $depsDir "tclap"
$tclapCachedTag = $null
if ((Test-Path (Join-Path $tclapDir "include")) -and (Test-Path (Join-Path $tclapDir ".git"))) {
    $tclapCachedTag = (git -C $tclapDir describe --tags --exact-match 2>$null)
    if ($LASTEXITCODE -ne 0) { $tclapCachedTag = $null; $global:LASTEXITCODE = 0 }
}
if ($tclapCachedTag -eq $tclapTag -and $tclapCachedTag) {
    Write-Host "  [cached] TCLAP already present at $tclapTag"
} else {
    if (Test-Path $tclapDir) {
        Write-Host "  Cached TCLAP is not at $tclapTag; re-cloning..."
        Remove-Item $tclapDir -Recurse -Force
    }
    git clone --depth 1 --branch $tclapTag https://github.com/TheFireKahuna/tclap $tclapDir
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone TCLAP" }
    Write-Host "  -> $tclapDir"
}

# --- 2b. Clone VST3 SDK (pluginterfaces only) ---
# VST3 hosting only needs the Steinberg pluginterfaces headers (COM-style interface
# definitions + the IID instantiation unit). public.sdk / vstgui / samples are not
# compiled, so we fetch just the pluginterfaces submodule. Pinned to the 3.8.0 tag,
# which is the first MIT-licensed release (compatible with our GPLv2-or-later code).
Write-Host "`n=== Step 2b: Clone VST3 SDK (pluginterfaces) ===" -ForegroundColor Yellow
$vst3Tag = $simdManifest.Shared.Vst3Tag
$vst3Dir = Join-Path $depsDir "vst3sdk"
$vst3InterfacesDir = Join-Path $vst3Dir "pluginterfaces"
if (Test-Path (Join-Path $vst3InterfacesDir "base\funknown.h")) {
    Write-Host "  [cached] VST3 pluginterfaces already present"
} else {
    New-Item -ItemType Directory -Force -Path $vst3Dir | Out-Null
    git clone --depth 1 --branch $vst3Tag https://github.com/steinbergmedia/vst3_pluginterfaces $vst3InterfacesDir
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone VST3 pluginterfaces" }
    Write-Host "  -> $vst3InterfacesDir"
}

# --- 2c. Clone Google Highway (header-only portable SIMD) ---
# The Common DSP kernels use Highway in static per-target dispatch mode, so only
# the headers are needed (no libhwy build, no runtime dispatch table).
Write-Host "`n=== Step 2c: Clone Highway ===" -ForegroundColor Yellow
$highwayTag = $simdManifest.Shared.HighwayTag
$highwayDir = Join-Path $depsDir "highway"
if (Test-Path (Join-Path $highwayDir "hwy\highway.h")) {
    Write-Host "  [cached] Highway already present"
} else {
    git clone --depth 1 --branch $highwayTag https://github.com/google/highway $highwayDir
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone Highway" }
    Write-Host "  -> $highwayDir"
}

# --- 3. Install Qt ---
if ($SkipQt) {
    Write-Host "`n=== Step 3: Qt Installation (SKIPPED) ===" -ForegroundColor Yellow
} else {
    Write-Host "`n=== Step 3: Install Qt $QtVersion ===" -ForegroundColor Yellow

    python -m pip install --upgrade "setuptools>=70.1.0" "py7zr==1.0.*" "aqtinstall==3.2.*" --quiet
    if ($LASTEXITCODE -ne 0) { throw "Failed to install aqtinstall" }

    $qtHost = if ($Platform -eq "ARM64") { "windows_arm64" } else { "windows" }
    $qtArch = if ($Platform -eq "ARM64") { "win64_msvc2022_arm64" } else { "win64_msvc2022_64" }
    $qtArchDir = if ($Platform -eq "ARM64") { "msvc2022_arm64" } else { "msvc2022_64" }
    $qtOutput = Join-Path $workspace "Qt"

    # Write aqt config to limit concurrency
    $aqtConfig = Join-Path $workspace "aqt-settings.ini"
    Set-Content -Path $aqtConfig -Encoding ASCII -Value @("[aqt]", "concurrency: 1")

    python -m aqt -c $aqtConfig install-qt $qtHost desktop $QtVersion $qtArch `
        --autodesktop `
        --outputdir $qtOutput `
        --archives qtbase qttools qtsvg qttranslations `
        --external 7z

    if ($LASTEXITCODE -ne 0) { throw "Qt installation failed" }

    $qtRoot = Join-Path $qtOutput "$QtVersion\$qtArchDir"
    if (-not (Test-Path (Join-Path $qtRoot "bin\qmake.exe"))) {
        throw "qmake.exe not found after Qt installation"
    }

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
