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

$projects = @(
    "SubwooferRoutingCore\SubwooferRoutingCore.vcxproj",
    "Common.vcxproj",
    "Tests\TestVst2Plugin\TestVst2Plugin.vcxproj",
    "VST3\SubwooferRouting\SubwooferRoutingVst3.vcxproj",
    "Tests\HybridConvTests\HybridConvTests.vcxproj",
    "Tests\EditorLogicTests\EditorLogicTests.vcxproj",
    "Tests\EngineOrchestrationTests\EngineOrchestrationTests.vcxproj",
    "Tests\AudioRegressionTests\AudioRegressionTests.vcxproj",
    "EqualizerAPO\EqualizerAPO.vcxproj",
    "Benchmark\Benchmark.vcxproj",
    "VoicemeeterClient\VoicemeeterClient.vcxproj"
)
$platformToolset = if ($Platform -eq "ARM64") { "v143" } else { "v145" }
$toolArchitecture = if ($Platform -eq "ARM64") { "ARM64" } else { "x64" }
# EditorLogicTests used to run everywhere because it linked no engine code and
# so carried only baseline instructions. It now links Common.lib whole-archive
# to get the filter factories' self-registration, which means it inherits the
# variant's /arch and executes a file-scope std::wregex construction before
# main. On a runner that cannot execute AVX-512 that is an illegal-instruction
# fault at static init, so it belongs behind the same gate as the others.
$runtimeTests = @()
if ($CanExecute) { $runtimeTests += @("EditorLogicTests", "HybridConvTests", "EngineOrchestrationTests") }
# The auto-detect installer is a Win32-only, CPU-baseline binary: it must run
# on machines with no AVX at all, so it takes no arch flag, and one build
# covers every variant. The avx2 leg builds it so a PR that breaks it fails
# in CI instead of on release day (audit #250 F056); the other legs skip the
# duplicate work, and create-release still builds its own copy to publish.
$installerProject = if ($Platform -eq "x64" -and $SimdVariant -eq "avx2") { "Installer\Installer.vcxproj" } else { $null }
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
"QT_ROOT=$qtRoot" >> $env:GITHUB_ENV
$msbuildPath = (Get-Command msbuild).Source
if ($Platform -eq "ARM64" -and $msbuildPath -notmatch '\\arm64\\') {
    throw "Expected native arm64 MSBuild, got $msbuildPath"
}
$buildParams = @(
    "/m", "/p:Configuration=$Configuration", "/p:Platform=$Platform", "/t:rebuild",
    "/p:LIBSNDFILE_INCLUDE=$env:LIBSNDFILE_INCLUDE", "/p:LIBSNDFILE_LIB=$env:LIBSNDFILE_LIB",
    "/p:FFTW_INCLUDE=$env:FFTW_INCLUDE", "/p:FFTW_LIB=$env:FFTW_LIB",
    "/p:MUPARSERX_INCLUDE=$env:MUPARSERX_INCLUDE", "/p:MUPARSERX_LIB=$env:MUPARSERX_LIB",
    "/p:TCLAP_ROOT=$env:TCLAP_ROOT", "/p:VST3_SDK=$env:VST3_SDK",
    "/p:HIGHWAY_INCLUDE=$env:HIGHWAY_INCLUDE", "/p:PlatformToolset=$platformToolset",
    "/p:PreferredToolArchitecture=$toolArchitecture", "/p:QT_ROOT=$qtRoot"
)
if ($Platform -eq "x64") { $buildParams += "/p:EnableEnhancedInstructionSet=$ArchFlag" }
foreach ($project in $projects) {
    msbuild $project @buildParams
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $project" }
}

if ($installerProject) {
    $installerParams = @(
        "/m", "/p:Configuration=$Configuration", "/p:Platform=Win32", "/t:rebuild",
        "/p:PlatformToolset=$platformToolset",
        "/p:PreferredToolArchitecture=$toolArchitecture"
    )
    msbuild $installerProject @installerParams
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $installerProject" }
}

$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$qtRoot\bin;$env:PATH"
foreach ($name in $runtimeTests) {
    $testExe = Join-Path $WorkspaceRoot "Tests\$name\$Platform\Release\$name.exe"
    if (-not (Test-Path $testExe)) { throw "Test executable not found: $testExe" }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
}
