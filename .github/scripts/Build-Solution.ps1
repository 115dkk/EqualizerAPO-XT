[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)] [ValidateSet("x64", "ARM64")] [string] $Platform,
    [string] $Configuration = "Release",
    [AllowEmptyString()] [string] $ArchFlag,
    [Parameter(Mandatory)] [string] $SimdVariant,
    [bool] $CanExecute = $true,
    [switch] $PlanOnly
)

# Audit #422 T1: the ordered list lives in BuildModel.psm1, tagged "solution".
# ASIO (docs/architecture/asio-host-study.md) and capture
# (docs/features/capture.md) probes are built on every leg so they cannot
# rot; their own gates execute them.
Import-Module (Join-Path $PSScriptRoot "BuildModel.psm1") -Force
$projects = @(Get-EapoProjectGraph -Tag solution | ForEach-Object Path)
# Both runner images ship VS 2026. The ARM64 image (windows-11-arm, which moved
# to windows-11-vs2026-arm64 in September 2026) keeps the v143 ARM64 compilers
# but carries ATL for ARM64 only for the default v145 toolset, so a v143 build
# of EqualizerAPO.dll stopped linking there (LNK1104 atls.lib).
$platformToolset = "v145"
$toolArchitecture = if ($Platform -eq "ARM64") { "ARM64" } else { "x64" }
# EditorLogicTests used to run everywhere because it linked no engine code and
# so carried only baseline instructions. It now links Common.lib whole-archive
# to get the filter factories' self-registration, which means it inherits the
# variant's /arch and executes a file-scope std::wregex construction before
# main. On a runner that cannot execute AVX-512 that is an illegal-instruction
# fault at static init, so it belongs behind the same gate as the others.
$runtimeTests = @()
if ($CanExecute) { $runtimeTests += @("EditorLogicTests", "HybridConvTests", "EngineOrchestrationTests", "AsioTests") }
# The auto-detect installer is a Win32-only, CPU-baseline binary: it must run
# on machines with no AVX at all, so it takes no arch flag, and one build
# covers every variant. The avx2 leg builds it so a PR that breaks it fails
# in CI instead of on release day (audit #250 F056); the other legs skip the
# duplicate work, and create-release still builds its own copy to publish.
$installerProject = if ($Platform -eq "x64" -and $SimdVariant -eq "avx2") { (Get-EapoProjectGraph -Tag installer).Path } else { $null }
$plan = [pscustomobject]@{
    Projects = $projects
    PlatformToolset = $platformToolset
    ToolArchitecture = $toolArchitecture
    RuntimeTests = $runtimeTests
    InstallerProject = $installerProject
}
if ($PlanOnly) { return $plan }

$ErrorActionPreference = "Stop"
Set-Location $WorkspaceRoot
$libPaths = @($env:LIBSNDFILE_LIB, $env:MUPARSERX_LIB, $env:FFTW_LIB)
$env:LIB = if ($env:LIB) { ($libPaths + $env:LIB) -join ";" } else { $libPaths -join ";" }

function Resolve-QtRoot {
    $candidates = @($env:QT_ROOT, $env:Qt6_DIR)
    if ($env:Qt6_DIR) { $candidates += (Join-Path $env:Qt6_DIR "..\..\..") }
    $qmake = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmake) { $candidates += (Split-Path (Split-Path $qmake.Source -Parent) -Parent) }
    $candidates += (Join-Path $WorkspaceRoot "Qt")
    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            $resolved = [System.IO.Path]::GetFullPath($candidate)
            if ((Test-Path (Join-Path $resolved "include\QtCore\QCoreApplication")) -and
                (Test-Path (Join-Path $resolved "lib\Qt6Core.lib")) -and
                (Test-Path (Join-Path $resolved "bin\Qt6Core.dll"))) { return $resolved }
        }
    }
    throw "Could not locate a complete Qt SDK"
}

$qtRoot = Resolve-QtRoot
if ($env:GITHUB_ENV) { "QT_ROOT=$qtRoot" >> $env:GITHUB_ENV }
$msbuildPath = (Get-Command msbuild).Source
if ($Platform -eq "ARM64" -and $msbuildPath -notmatch '\\arm64\\') {
    throw "Expected native arm64 MSBuild, got $msbuildPath"
}
$env:QT_ROOT = $qtRoot
$buildParams = Get-EapoMsBuildArguments -Platform $Platform -Configuration $Configuration `
    -ArchFlag $ArchFlag -PlatformToolset $platformToolset -ToolArchitecture $toolArchitecture
Invoke-EapoMsBuild -Project $projects -Arguments $buildParams

if ($installerProject) {
    $installerParams = Get-EapoMsBuildArguments -Platform Win32 -Configuration $Configuration `
        -PlatformToolset $platformToolset -ToolArchitecture $toolArchitecture -Dependencies None
    Invoke-EapoMsBuild -Project $installerProject -Arguments $installerParams
}

$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$qtRoot\bin;$env:PATH"
foreach ($name in $runtimeTests) {
    $testExe = Join-Path $WorkspaceRoot "Tests\$name\$Platform\Release\$name.exe"
    if (-not (Test-Path $testExe)) { throw "Test executable not found: $testExe" }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
}
