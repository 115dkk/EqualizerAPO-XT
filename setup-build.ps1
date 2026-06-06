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

# velopack_libc ships as a single cross-platform zip attached to the velopack/velopack
# release. Pin the version so the asset name and download URL stay in sync with CI.
$velopackLibcVersion = "1.1.1"

Write-Host "=== EqualizerAPO-XT Build Setup ===" -ForegroundColor Cyan
Write-Host "Workspace: $workspace"
Write-Host "Platform:  $Platform"
Write-Host "SIMD:      $SimdVariant"
Write-Host ""

# --- Dependency asset mapping (mirrors build.yml matrix) ---
$assetMap = @{
    "x64" = @{
        "sse2"     = @{ muparserx = "muparserx-msvc-release-x64-avx2.zip" }
        "avx"      = @{ muparserx = "muparserx-msvc-release-x64-avx2.zip" }
        "avx2"     = @{
            fftw      = "fftw-windows-release-x64-avx2.zip"
            muparserx = "muparserx-msvc-release-x64-avx2.zip"
            sndfile   = "libsndfile-x64-avx2.zip"
        }
        "avx512"   = @{
            fftw      = "fftw-windows-release-x64-avx512.zip"
            muparserx = "muparserx-msvc-release-x64-avx512.zip"
            sndfile   = "libsndfile-x64-avx512.zip"
        }
        "avx10_1"  = @{
            fftw      = "fftw-windows-release-x64-avx10.zip"
            muparserx = "muparserx-msvc-release-x64-avx10.zip"
            sndfile   = "libsndfile-x64-avx10.zip"
        }
    }
    "ARM64" = @{
        "neon" = @{
            fftw      = "fftw-windows-release-arm64.zip"
            muparserx = "muparserx-msvc-release-ARM64.zip"
            sndfile   = "libsndfile-arm64.zip"
        }
    }
}

$variant = if ($Platform -eq "ARM64") { "neon" } else { $SimdVariant }
$assets = $assetMap[$Platform][$variant]
if (-not $assets) {
    throw "No asset mapping for Platform=$Platform, Variant=$variant"
}

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
        Repo        = "velopack/velopack"
        Asset       = "velopack_libc_$velopackLibcVersion.zip"
        Url         = "https://github.com/velopack/velopack/releases/download/$velopackLibcVersion/velopack_libc_$velopackLibcVersion.zip"
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
    $url = if ($dl.Url) { $dl.Url } else { "https://github.com/$($dl.Repo)/releases/latest/download/$($dl.Asset)" }
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

    New-Item -ItemType Directory -Force -Path $dl.Destination | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $dl.Destination -Force
    Write-Host "  -> $($dl.Destination)"
}

# For SSE2/AVX variants, build FFTW and libsndfile from vcpkg
if ($usesVcpkg) {
    Write-Host "`n  Building lower-SIMD dependencies via vcpkg..."

    # Import VS dev environment
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $vsInstallPath = & $vswherePath -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -latest
    if (-not $vsInstallPath) {
        $vsInstallPath = & $vswherePath -products * -requires Microsoft.Component.MSBuild -property installationPath -latest
    }
    $devCmdPath = Join-Path $vsInstallPath "Common7\Tools\VsDevCmd.bat"
    $environment = cmd /c "`"$devCmdPath`" -arch=x64 -no_logo >nul && set"
    foreach ($line in $environment) {
        if ($line -match "^(.*?)=(.*)$") {
            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
        }
    }

    $vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
    if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
        $vcpkgRoot = Join-Path $workspace "vcpkg"
        if (-not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
            git clone --depth 1 https://github.com/microsoft/vcpkg $vcpkgRoot
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
Write-Host "`n=== Step 2: Clone TCLAP ===" -ForegroundColor Yellow
$tclapDir = Join-Path $depsDir "tclap"
if (Test-Path (Join-Path $tclapDir "include")) {
    Write-Host "  [cached] TCLAP already present"
} else {
    git clone --depth 1 https://github.com/TheFireKahuna/tclap $tclapDir
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone TCLAP" }
    Write-Host "  -> $tclapDir"
}

# --- 2b. Clone VST3 SDK (pluginterfaces only) ---
# VST3 hosting only needs the Steinberg pluginterfaces headers (COM-style interface
# definitions + the IID instantiation unit). public.sdk / vstgui / samples are not
# compiled, so we fetch just the pluginterfaces submodule. Pinned to the 3.8.0 tag,
# which is the first MIT-licensed release (compatible with our GPLv2-or-later code).
Write-Host "`n=== Step 2b: Clone VST3 SDK (pluginterfaces) ===" -ForegroundColor Yellow
$vst3Tag = "v3.8.0_build_66"
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
    @{ Name = "VST3 pluginterfaces"; Path = "$depsDir\vst3sdk\pluginterfaces\base\funknown.h" }
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
  `$env:QT_ROOT          = "$qtRoot"

  # Build core projects with MSBuild
  msbuild EqualizerAPO.sln /p:Configuration=Release /p:Platform=x64

  # Build Qt apps with qmake
  cd build-Editor
  qmake ..\Editor\Editor.pro -r CONFIG+=release
  nmake
"@
} else {
    Write-Host "`n=== Setup Incomplete ===" -ForegroundColor Red
    Write-Host "Some dependencies are missing. Check the output above."
    exit 1
}
