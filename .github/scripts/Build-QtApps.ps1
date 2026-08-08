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
$requiredExes = @("Editor", "DeviceSelector", "UpdateChecker") |
    ForEach-Object { "build-$_-$Platform\release\$_.exe" }
$plan = [pscustomobject]@{ UpdateChannel = $updateChannel; RequiredExecutables = $requiredExes }
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
    $buildDir = Join-Path $WorkspaceRoot "build-$name-$Platform"
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
    }
    finally { Pop-Location }
}

Build-QtProject "Editor" "Editor\Editor.pro"
Build-QtProject "DeviceSelector" "DeviceSelector\DeviceSelector.pro"
Build-QtProject "UpdateChecker" "UpdateChecker\UpdateChecker.pro"
$missing = @($requiredExes | Where-Object { -not (Test-Path (Join-Path $WorkspaceRoot $_)) })
if ($missing.Count) { throw "Qt build did not produce: $($missing -join ', ')" }
