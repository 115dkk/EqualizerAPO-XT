<#
.SYNOPSIS
    Sets up the build environment for EqualizerAPO-XT.
    Downloads external dependencies and installs Qt 6.

.DESCRIPTION
    A developer-facing wrapper around .github\scripts\Initialize-Workspace.ps1,
    the entry point every CI job uses, so a local checkout is prepared by the
    same steps in the same order (TECH_DEBT_AUDIT TD-35, audit #467):
    1. Downloads FFTW, muparserx, libsndfile, velopack_libc and the ASIO SDK
    2. Clones the TCLAP header-only library, the VST3 SDK pluginterfaces and Highway
    3. Installs Qt (the manifest's Shared.QtVersion) from Qt's online repository
    4. Exports the dependency paths, QT_ROOT and Qt's bin folder on PATH to
       this PowerShell session (and to GITHUB_ENV / GITHUB_PATH under Actions)

    This wrapper adds what a person wants on top: cached downloads are reused,
    the result is verified file by file, and the two build calls are printed.

.PARAMETER SkipQt
    Skip Qt installation (useful if Qt is already installed).

.PARAMETER QtVersion
    Qt version to install. Default: Shared.QtVersion in .github/simd-variants.psd1

.PARAMETER SimdVariant
    SIMD variant for dependencies. Default: the manifest's Primary variant (avx2)
    Options: sse2, avx, avx2, avx512, avx10_1

.PARAMETER Platform
    Target platform. Default: x64
    Options: x64, ARM64
#>
param(
    [switch]$SkipQt,
    # Audit #250 F059: empty means "use Shared.QtVersion from the manifest".
    [string]$QtVersion = "",
    # Empty means the manifest's Primary variant.
    [ValidateSet("", "sse2", "avx", "avx2", "avx512", "avx10_1")]
    [string]$SimdVariant = "",
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$workspace = $PSScriptRoot
$initialize = Join-Path $workspace ".github\scripts\Initialize-Workspace.ps1"

$plan = & $initialize -WorkspaceRoot $workspace -Platform $Platform -Simd $SimdVariant `
    -QtVersion $QtVersion -SkipQt:$SkipQt -PlanOnly
$depsDir = $plan.DepsRoot
$qtRoot = $plan.QtRoot

Write-Host "=== EqualizerAPO-XT Build Setup ===" -ForegroundColor Cyan
Write-Host "Workspace: $workspace"
Write-Host "Platform:  $($plan.Platform)"
Write-Host "SIMD:      $($plan.Simd)"
Write-Host ""

# A cached zip in deps\_downloads is reused, and a mismatching one is deleted
# so the next run redownloads it.
& $initialize -WorkspaceRoot $workspace -Platform $Platform -Simd $SimdVariant `
    -QtVersion $QtVersion -SkipQt:$SkipQt -ReuseCachedDownloads

# --- Verify ---
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
    @{ Name = "Highway header";  Path = "$depsDir\highway\hwy\highway.h" },
    @{ Name = "ASIO SDK header"; Path = "$depsDir\asiosdk\ASIOSDK\common\iasiodrv.h" }
)

# velopack_libc ships per-arch import libs/DLLs in a single zip; check the one we link.
$velopackArch = if ($Platform -eq "ARM64") { "arm64" } else { "x64" }
$checks += @(
    @{ Name = "Velopack header"; Path = "$depsDir\velopack_libc\include\Velopack.hpp" },
    @{ Name = "Velopack import lib"; Path = "$depsDir\velopack_libc\lib\velopack_libc_win_${velopackArch}_msvc.dll.lib" },
    @{ Name = "Velopack DLL";    Path = "$depsDir\velopack_libc\lib\velopack_libc_win_${velopackArch}_msvc.dll" }
)

if (-not $SkipQt) {
    $checks += @(
        @{ Name = "qmake";    Path = "$qtRoot\bin\qmake.exe" },
        @{ Name = "Qt6Core";  Path = "$qtRoot\lib\Qt6Core.lib" },
        @{ Name = "lrelease"; Path = "$qtRoot\bin\lrelease.exe" }
    )
}

$allOk = $true
foreach ($check in $checks) {
    if (Test-Path $check.Path) {
        Write-Host "  [OK] $($check.Name)" -ForegroundColor Green
    } else {
        Write-Host "  [MISSING] $($check.Name): $($check.Path)" -ForegroundColor Red
        $allOk = $false
    }
}

if ($allOk) {
    Write-Host "`n=== Setup Complete ===" -ForegroundColor Green

    # Audit #422 TD-47: print the calls CI makes, not a recipe of its own.
    # The old recipe built EqualizerAPO.sln (a different project graph, with
    # Debug test projects against libraries deps\ does not ship) and ran
    # qmake/nmake without the CONFIG+=force_debug_info and windeployqt steps
    # Build-QtApps.ps1 adds, so a local Editor differed from the shipped one.
    # The scripts read the arch flags, the update channel and the dependency
    # and Qt locations from the manifest and the checkout, so the two calls
    # name the platform and the variant and nothing else.
    Write-Host @"

To build exactly what CI builds, open a VS Developer PowerShell (msbuild on
PATH) in $workspace and run:

  .\.github\scripts\Build-Solution.ps1 -WorkspaceRoot "$workspace" -Platform $($plan.Platform) -SimdVariant $($plan.Simd)
  .\.github\scripts\Build-QtApps.ps1 -WorkspaceRoot "$workspace" -Platform $($plan.Platform) -SimdVariant $($plan.Simd)

Build-Solution.ps1 builds every MSBuild project and runs the runtime suites;
Build-QtApps.ps1 builds the Editor and DeviceSelector with qmake.
"@
} else {
    Write-Host "`n=== Setup Incomplete ===" -ForegroundColor Red
    Write-Host "Some dependencies are missing. Check the output above."
    exit 1
}
