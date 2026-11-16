[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)] [ValidateSet("x64", "ARM64")] [string] $Platform,
    [string] $Configuration = "Release",
    # Empty means "the manifest's value for this variant". CI passes
    # matrix.arch_flag, which New-BuildMatrix.ps1 expanded from the same
    # manifest; a local or gate call names the platform and variant only.
    [AllowEmptyString()] [string] $ArchFlag,
    # Empty means the manifest's Primary variant (TECH_DEBT_AUDIT TD-35).
    [AllowEmptyString()] [string] $SimdVariant,
    [bool] $CanExecute = $true,
    [switch] $PlanOnly
)

Import-Module (Join-Path $PSScriptRoot "Provisioning.psm1") -Force
$manifest = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot ".." "simd-variants.psd1")
$variant = Resolve-BuildVariant -Manifest $manifest -Platform $Platform -Simd $SimdVariant
if (-not $PSBoundParameters.ContainsKey("ArchFlag")) { $ArchFlag = $variant.MsBuildArchFlag }

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
# covers every variant. The Primary leg builds it so a PR that breaks it fails
# in CI instead of on release day (audit #250 F056); the other legs skip the
# duplicate work, and create-release still builds its own copy to publish.
$installerProject = if ($variant.IsPrimary) { (Get-EapoProjectGraph -Tag installer).Path } else { $null }
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
# A fresh shell on a prepared checkout has no dependency environment yet
# (Initialize-Workspace.ps1 exported it to the session that ran it). Derive
# it from deps\ instead of failing later with empty /p: values.
if (-not $env:FFTW_LIB -and (Test-Path (Join-Path $WorkspaceRoot "deps"))) {
    & (Join-Path $PSScriptRoot "Initialize-DepsEnvironment.ps1") -DepsPath (Join-Path $WorkspaceRoot "deps") | Out-Null
}
$libPaths = @($env:LIBSNDFILE_LIB, $env:MUPARSERX_LIB, $env:FFTW_LIB)
$env:LIB = if ($env:LIB) { ($libPaths + $env:LIB) -join ";" } else { $libPaths -join ";" }

function Resolve-QtRoot {
    $candidates = @($env:QT_ROOT, $env:Qt6_DIR)
    if ($env:Qt6_DIR) { $candidates += (Join-Path $env:Qt6_DIR "..\..\..") }
    $qmake = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmake) { $candidates += (Split-Path (Split-Path $qmake.Source -Parent) -Parent) }
    # Where Initialize-Workspace.ps1 / setup-build.ps1 install it. The last
    # candidate used to be <root>\Qt, which never holds an SDK, so a checkout
    # prepared by setup-build.ps1 stopped here (TECH_DEBT_AUDIT TD-71).
    $candidates += (Join-Path $WorkspaceRoot "Qt\$($manifest.Shared.QtVersion)\$($variant.QtArchDir)")
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

    # The installer README sends every user to was built here and never run
    # (TECH_DEBT_AUDIT TD-25, audit #467). --detect-only runs its CPU
    # detection and channel choice and stops before downloading: the channel
    # it names must be one the manifest publishes, and the URL must end in
    # that channel's Setup asset name, or a renamed asset would only be found
    # by users. The exit code is the channel index, so it is not checked.
    if ($CanExecute) {
        Import-Module (Join-Path $PSScriptRoot "ReleaseAssets.psm1") -Force
        Import-Module (Join-Path $PSScriptRoot "ApoEndpointHarness.psm1") -Force
        $installerExe = Join-Path $WorkspaceRoot "Installer\Release\$(Get-UniversalSetupAssetName)"
        if (-not (Test-Path $installerExe)) { throw "Installer not found: $installerExe" }
        $detectionFile = Join-Path ([System.IO.Path]::GetTempPath()) "eapo-installer-detection.txt"
        Remove-Item -LiteralPath $detectionFile -Force -ErrorAction SilentlyContinue
        $run = Invoke-ExternalProgram $installerExe @("--detect-only", "--out", $detectionFile) 120 $null
        if ($run.TimedOut) { throw "The installer's --detect-only did not return within 120 s" }
        if (-not (Test-Path $detectionFile)) { throw "The installer's --detect-only wrote no result (exit $($run.ExitCode))" }
        $detected = @(Get-Content -LiteralPath $detectionFile | Where-Object { $_.Trim() })
        $detectedChannel = "$($detected[0])".Trim()
        $detectedUrl = "$($detected[1])".Trim()
        $published = @($manifest.Variants | ForEach-Object { $_.Channel })
        if ($published -notcontains $detectedChannel) {
            throw "The installer chose channel '$detectedChannel', which simd-variants.psd1 does not publish ($($published -join ', '))"
        }
        $expectedAsset = Get-SetupAssetName -Channel $detectedChannel
        if (-not $detectedUrl.EndsWith("/$expectedAsset")) {
            throw "The installer would download '$detectedUrl'; the release asset for $detectedChannel is named $expectedAsset"
        }
        Write-Host "Installer detection: $detectedChannel -> $detectedUrl"
    }
}

$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$qtRoot\bin;$env:PATH"
foreach ($name in $runtimeTests) {
    $testExe = Join-Path $WorkspaceRoot "Tests\$name\$Platform\Release\$name.exe"
    if (-not (Test-Path $testExe)) { throw "Test executable not found: $testExe" }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
}
