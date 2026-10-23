[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)] [ValidateSet("x64", "ARM64")] [string] $Platform,
    [Parameter(Mandatory)] [string] $SimdVariant,
    [AllowEmptyString()] [string] $QtArchFlag,
    [Parameter(Mandatory)] [string] $MsvcDevPlatform,
    # Audit #250 F066: CI passes matrix.channel; empty means "look it up in
    # the manifest" (local runs). The old string convention
    # ("x64-" + simd with _ -> -) could silently drift from the manifest's
    # Channel field and publish under a different label.
    [AllowEmptyString()] [string] $UpdateChannel,
    [switch] $AddressSanitizer,
    [switch] $PlanOnly
)

if (-not $UpdateChannel) {
    $manifest = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot ".." "simd-variants.psd1")
    $lookupSimd = if ($Platform -eq "ARM64") { "neon" } else { $SimdVariant }
    $entry = $manifest.Variants | Where-Object { $_.Platform -eq $Platform -and $_.Simd -eq $lookupSimd } | Select-Object -First 1
    if (-not $entry) { throw "No variant in simd-variants.psd1 for $Platform/$lookupSimd" }
    $UpdateChannel = $entry.Channel
}
$updateChannel = $UpdateChannel
if ($AddressSanitizer -and $Platform -ne "x64") {
    throw "AddressSanitizer Qt builds support x64 only"
}
$asanQmakeArguments = @(
    "QMAKE_CFLAGS+=/fsanitize=address",
    "QMAKE_CXXFLAGS+=/fsanitize=address",
    "DEFINES+=_DISABLE_STRING_ANNOTATION",
    "DEFINES+=_DISABLE_VECTOR_ANNOTATION",
    "QMAKE_LFLAGS+=/INCREMENTAL:NO"
)
if ($AddressSanitizer) {
    [string[]] $apps = @("Editor")
    [string[]] $extraQmakeArguments = $asanQmakeArguments
} else {
    [string[]] $apps = @("Editor", "DeviceSelector")
    [string[]] $extraQmakeArguments = @()
}
$buildDirectories = [ordered]@{}
foreach ($app in $apps) {
    $suffix = if ($AddressSanitizer -and $app -eq "Editor") { "-asan" } else { "" }
    $buildDirectories[$app] = "build-$app-$Platform$suffix"
}
$requiredExes = @($apps | ForEach-Object { "$($buildDirectories[$_])\release\$_.exe" })
$plan = [pscustomobject]@{
    UpdateChannel = $updateChannel
    RequiredExecutables = $requiredExes
    Apps = $apps
    BuildDirectories = $buildDirectories
    ExtraQmakeArguments = $extraQmakeArguments
}
if ($PlanOnly) { return $plan }

$ErrorActionPreference = "Stop"
Set-Location $WorkspaceRoot
. (Join-Path $WorkspaceRoot ".github\scripts\Import-VsDevEnvironment.ps1")
Import-VsDevEnvironment $MsvcDevPlatform

function Resolve-QtTool([string] $name) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($root in @($env:QT_ROOT, $env:Qt6_DIR)) {
        if ($root) {
            $candidate = Join-Path $root "bin\$name.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw "Could not locate $name"
}
$qmake = Resolve-QtTool "qmake"
$lrelease = Resolve-QtTool "lrelease"
$windeployqt = Resolve-QtTool "windeployqt"
$buildTool = if (Get-Command jom -ErrorAction SilentlyContinue) { "jom" } else { "nmake" }

function Build-QtProject([string] $name, [string] $project) {
    $buildDir = Join-Path $WorkspaceRoot $buildDirectories[$name]
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    Push-Location $buildDir
    try {
        & $lrelease "..\$project"
        if ($LASTEXITCODE -ne 0) { throw "$name lrelease failed" }
        $args = @("..\$project", "-r", "CONFIG+=release", "CONFIG+=force_debug_info",
            "EAPO_UPDATE_CHANNEL=$updateChannel")
        if ($Platform -eq "x64") {
            $args += if ($QtArchFlag) { "EAPO_SIMD_FLAGS=$QtArchFlag" } else { "EAPO_SIMD_BASELINE=1" }
        }
        # The prebuilt muparserx, libsndfile, FFTW and Velopack libraries are
        # not ASan-instrumented. Disable the std container annotations to avoid
        # LNK2038, as Invoke-MemcheckTests.ps1 does through CL. Incremental
        # linking is incompatible with ASan.
        $args += $extraQmakeArguments
        & $qmake @args
        if ($LASTEXITCODE -ne 0) { throw "$name qmake failed" }
        & $buildTool
        if ($LASTEXITCODE -ne 0) { throw "$name build failed" }
        $exe = "release\$name.exe"
        if (-not (Test-Path $exe)) { throw "$name executable was not produced" }
        & $windeployqt $exe --release --no-opengl-sw
        if ($LASTEXITCODE -ne 0) { throw "$name windeployqt failed" }
        if (-not (Test-Path "release\platforms\qwindows.dll")) {
            throw "$name deployment is missing platforms\qwindows.dll"
        }
        if ($AddressSanitizer) {
            $runtimeName = "clang_rt.asan_dynamic-x86_64.dll"
            $runtime = $null
            foreach ($pathDirectory in $env:PATH -split [System.IO.Path]::PathSeparator) {
                if (-not $pathDirectory) { continue }
                $candidate = Join-Path $pathDirectory $runtimeName
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    $runtime = $candidate
                    break
                }
            }
            if (-not $runtime -and $env:VCToolsInstallDir) {
                $candidate = Join-Path $env:VCToolsInstallDir "bin\Hostx64\x64\$runtimeName"
                if (Test-Path -LiteralPath $candidate -PathType Leaf) { $runtime = $candidate }
            }
            if (-not $runtime) { throw "Could not locate $runtimeName" }
            Copy-Item -LiteralPath $runtime -Destination "release\$runtimeName" -Force
        }
    }
    finally { Pop-Location }
}

foreach ($app in $apps) {
    Build-QtProject $app "$app\$app.pro"
}
$missing = @($requiredExes | Where-Object { -not (Test-Path (Join-Path $WorkspaceRoot $_)) })
if ($missing.Count) { throw "Qt build did not produce: $($missing -join ', ')" }
