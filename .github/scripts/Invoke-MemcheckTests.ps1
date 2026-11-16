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

    The APO DLL itself (audit #467 TD-26). The suites compile the engine,
    not the COM shell around it, so the DLL the audio engine loads never ran
    under the sanitizer. It cannot run here either: hosting it needs an
    endpoint's record and a config in HKLM, which only the capture gate's
    runner has. So this script builds EqualizerAPO.dll and Tests/ApoHostProbe
    with the same flags and, given -ApoExportDirectory, copies them there
    with the sanitizer runtime and the DLL's dependencies; build.yml uploads
    that folder and the capture gate runs its hosted rounds on it
    (Invoke-CaptureGate.ps1 -AsanApoDirectory). The instrumented DLL is only
    ever loaded by the probe, never registered and never given to the audio
    engine.

    -PlanOnly returns the projects, the suites and their arguments, for
    Pester.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [ValidateRange(1, 1440)] [int] $SuiteTimeoutMinutes = 30,
    # Where to put the instrumented APO and its probe for the capture gate.
    # Empty: they are still built (a build break shows here), not exported.
    [string] $ApoExportDirectory,
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"

# The gate runs the manifest's Primary variant; nothing here spells its name
# or its arch flag (TECH_DEBT_AUDIT TD-35).
Import-Module (Join-Path $PSScriptRoot "Provisioning.psm1") -Force
$variant = Resolve-BuildVariant -Platform x64 -Simd "" `
    -Manifest (Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot ".." "simd-variants.psd1"))
$solutionPlan = & (Join-Path $PSScriptRoot "Build-Solution.ps1") `
    -WorkspaceRoot $WorkspaceRoot -Platform x64 -SimdVariant $variant.Simd -CanExecute $true -PlanOnly
$suites = @($solutionPlan.RuntimeTests) + @("AudioRegressionTests")
$suiteArguments = @{
    AudioRegressionTests = @(
        "--variant", $variant.Simd,
        "--config-dir", (Join-Path $WorkspaceRoot "Tests\AudioRegressionTests\configs"),
        "--ref-dir", (Join-Path $WorkspaceRoot "Tests\AudioRegressionTests\references"),
        "--out-dir", (Join-Path ([System.IO.Path]::GetTempPath()) "eapo-memcheck-regression")
    )
}
# The suite exes plus everything they load: the companion VST modules must be
# part of the same ASan build so their objects agree with the host's.
Import-Module (Join-Path $PSScriptRoot "BuildModel.psm1") -Force
$apoHostedProjects = @("EqualizerAPO", "ApoHostProbe")
$projects = @(Get-EapoProjectGraph -Tag engine | ForEach-Object Path) +
    @(Get-EapoProjectGraph -Name (@("TestVst2Plugin", "TestChildApo", "SubwooferRoutingVst3") + $suites + $apoHostedProjects) | ForEach-Object Path)
# What the capture gate needs beside each other: the instrumented pair, the
# sanitizer runtime the toolset ships, and the DLLs EqualizerAPO.dll imports.
$apoExport = [pscustomobject]@{
    Directory = $ApoExportDirectory
    Binaries = @(
        (Join-Path $WorkspaceRoot "EqualizerAPO\x64\Release\EqualizerAPO.dll"),
        (Join-Path $WorkspaceRoot "Tests\ApoHostProbe\x64\Release\ApoHostProbe.exe")
    )
    SanitizerRuntime = "clang_rt.asan_dynamic-x86_64.dll"
    DependencyVariables = @("FFTW_LIB", "LIBSNDFILE_LIB", "MUPARSERX_LIB")
}
$plan = [pscustomobject]@{
    Projects = $projects
    Suites = $suites
    SuiteArguments = $suiteArguments
    SuiteTimeoutMinutes = $SuiteTimeoutMinutes
    ApoHostedProjects = $apoHostedProjects
    ApoExport = $apoExport
}
if ($PlanOnly) { return $plan }

Set-Location $WorkspaceRoot

$libPaths = @($env:LIBSNDFILE_LIB, $env:MUPARSERX_LIB, $env:FFTW_LIB)
$env:LIB = if ($env:LIB) { ($libPaths + $env:LIB) -join ";" } else { $libPaths -join ";" }
$env:CL = "/D_DISABLE_STRING_ANNOTATION /D_DISABLE_VECTOR_ANNOTATION"

$buildParams = @(Get-EapoMsBuildArguments -Platform x64 -ArchFlag $variant.MsBuildArchFlag) +
    @("/p:EnableASAN=true", "/p:WholeProgramOptimization=false")
Invoke-EapoMsBuild -Project $projects -Arguments $buildParams -FailureLabel "ASan build"
Remove-Item Env:CL

if ($apoExport.Directory) {
    New-Item -ItemType Directory -Force -Path $apoExport.Directory | Out-Null
    foreach ($binary in $apoExport.Binaries) {
        if (-not (Test-Path -LiteralPath $binary)) { throw "ASan build left no $binary" }
        Copy-Item -LiteralPath $binary -Destination $apoExport.Directory -Force
    }
    # The runtime sits beside cl.exe (Import-VsDevEnvironment put that folder
    # on PATH). Without it the probe does not start on the capture gate's
    # runner, which has no such toolset.
    $runtime = Get-Command $apoExport.SanitizerRuntime -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $runtime) { throw "$($apoExport.SanitizerRuntime) is not on PATH; run Import-VsDevEnvironment first" }
    Copy-Item -LiteralPath $runtime.Source -Destination $apoExport.Directory -Force
    foreach ($variable in $apoExport.DependencyVariables) {
        $folder = [Environment]::GetEnvironmentVariable($variable)
        if ($folder -and (Test-Path -LiteralPath $folder)) {
            Get-ChildItem -LiteralPath $folder -Filter "*.dll" -File |
                Copy-Item -Destination $apoExport.Directory -Force
        }
    }
    Write-Host "Exported the instrumented APO to $($apoExport.Directory):"
    Get-ChildItem -LiteralPath $apoExport.Directory -File | ForEach-Object { Write-Host "  $($_.Name) ($($_.Length) bytes)" }
}

$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$env:MUPARSERX_LIB;$env:QT_ROOT\bin;$env:PATH"
Import-Module (Join-Path $PSScriptRoot "TestExecution.psm1") -Force
foreach ($name in $suites) {
    $testExe = Join-Path $WorkspaceRoot "Tests\$name\x64\Release\$name.exe"
    if (-not (Test-Path $testExe)) { throw "Test executable not found: $testExe" }
    Write-Host "== memcheck: $name =="
    $arguments = if ($suiteArguments.ContainsKey($name)) { $suiteArguments[$name] } else { @() }
    Invoke-TestExecutable -Name $name -FilePath $testExe -Arguments $arguments -TimeoutSeconds ($SuiteTimeoutMinutes * 60)
}
