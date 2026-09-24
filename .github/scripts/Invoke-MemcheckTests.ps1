<#
.SYNOPSIS
    CI's dynamic memory gate: rebuilds the runtime test suites with MSVC
    AddressSanitizer and runs them; any heap violation or an unbalanced
    AlignedMemory counter turns the job red.

.DESCRIPTION
    Two detection axes, because MSVC has no LeakSanitizer on Windows:

    - AddressSanitizer (/fsanitize=address via /p:EnableASAN=true) reports
      use-after-free, heap/stack overflows and double frees at the moment
      they happen; the report aborts the process, so the run step fails.
    - The AlignedMemory leak canary (Tests/AlignedMemoryGate.h) makes each
      suite binary exit non-zero when engine buffer allocations do not
      balance at the end of the run. It runs in every build flavor; this
      job simply exercises it on the same binaries.

    Build notes (mirrored from the local experiment, 2026-08-22):
    - The prebuilt dependency libs (muparserx, libsndfile, FFTW) are not
      ASan-instrumented, so the std container annotation checks must be
      disabled (_DISABLE_STRING_ANNOTATION/_DISABLE_VECTOR_ANNOTATION via
      the CL environment) or the link fails with LNK2038.
    - Whole-program optimization is turned off; ASan and /GL disagree.
    - Timing contracts skip themselves under ASan (__SANITIZE_ADDRESS__,
      see SampleIoTests.cpp): instrumentation cost lands on the vectorized
      candidate and would invert every ratio.
    - The ASan runtime DLL ships in the VC toolset bin directory, which
      Import-VsDevEnvironment puts on PATH before the run.

    Which suites run is not written here (audit #348 D1/TD-22). The runtime
    suites are the ones Build-Solution.ps1 runs on an executing leg, read
    from its plan, plus AudioRegressionTests, which build.yml runs in its
    own step because it needs its reference arguments. The list used to be
    typed twice and had already drifted: AsioTests was rebuilt with ASan but
    never run, and AudioRegressionTests (the widest walk through the engine
    buffers) was in neither. BuildScripts.Tests.ps1 pins the two lists.

    -PlanOnly returns the projects, the suites and their arguments, for
    Pester.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"

$solutionPlan = & (Join-Path $PSScriptRoot "Build-Solution.ps1") `
    -WorkspaceRoot $WorkspaceRoot -Platform x64 -SimdVariant avx2 `
    -ArchFlag AdvancedVectorExtensions2 -CanExecute $true -PlanOnly
$suites = @($solutionPlan.RuntimeTests) + @("AudioRegressionTests")
$suiteArguments = @{
    AudioRegressionTests = @(
        "--variant", "avx2",
        "--config-dir", (Join-Path $WorkspaceRoot "Tests\AudioRegressionTests\configs"),
        "--ref-dir", (Join-Path $WorkspaceRoot "Tests\AudioRegressionTests\references"),
        "--out-dir", (Join-Path ([System.IO.Path]::GetTempPath()) "eapo-memcheck-regression")
    )
}
# The suite exes plus everything they load: the companion VST modules must be
# part of the same ASan build so their objects agree with the host's.
$projects = @(
    "SubwooferRoutingCore\SubwooferRoutingCore.vcxproj",
    "Common.vcxproj",
    "Tests\TestVst2Plugin\TestVst2Plugin.vcxproj",
    "VST3\SubwooferRouting\SubwooferRoutingVst3.vcxproj"
) + @($suites | ForEach-Object { "Tests\$_\$_.vcxproj" })
$plan = [pscustomobject]@{
    Projects = $projects
    Suites = $suites
    SuiteArguments = $suiteArguments
}
if ($PlanOnly) { return $plan }

Set-Location $WorkspaceRoot

$libPaths = @($env:LIBSNDFILE_LIB, $env:MUPARSERX_LIB, $env:FFTW_LIB)
$env:LIB = if ($env:LIB) { ($libPaths + $env:LIB) -join ";" } else { $libPaths -join ";" }
$env:CL = "/D_DISABLE_STRING_ANNOTATION /D_DISABLE_VECTOR_ANNOTATION"

$buildParams = @(
    "/m", "/p:Configuration=Release", "/p:Platform=x64", "/t:rebuild",
    "/p:EnableASAN=true", "/p:WholeProgramOptimization=false",
    "/p:EnableEnhancedInstructionSet=AdvancedVectorExtensions2",
    "/p:PlatformToolset=v145",
    "/p:LIBSNDFILE_INCLUDE=$env:LIBSNDFILE_INCLUDE", "/p:LIBSNDFILE_LIB=$env:LIBSNDFILE_LIB",
    "/p:FFTW_INCLUDE=$env:FFTW_INCLUDE", "/p:FFTW_LIB=$env:FFTW_LIB",
    "/p:MUPARSERX_INCLUDE=$env:MUPARSERX_INCLUDE", "/p:MUPARSERX_LIB=$env:MUPARSERX_LIB",
    "/p:TCLAP_ROOT=$env:TCLAP_ROOT", "/p:VST3_SDK=$env:VST3_SDK",
    "/p:HIGHWAY_INCLUDE=$env:HIGHWAY_INCLUDE", "/p:ASIO_SDK=$env:ASIO_SDK",
    "/p:QT_ROOT=$env:QT_ROOT"
)

foreach ($project in $projects) {
    msbuild $project @buildParams
    if ($LASTEXITCODE -ne 0) { throw "ASan build failed for $project" }
}
Remove-Item Env:CL

$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$env:MUPARSERX_LIB;$env:QT_ROOT\bin;$env:PATH"
foreach ($name in $suites) {
    $testExe = Join-Path $WorkspaceRoot "Tests\$name\x64\Release\$name.exe"
    if (-not (Test-Path $testExe)) { throw "Test executable not found: $testExe" }
    Write-Host "== memcheck: $name =="
    $arguments = if ($suiteArguments.ContainsKey($name)) { $suiteArguments[$name] } else { @() }
    & $testExe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$name failed under the memory gate" }
}
