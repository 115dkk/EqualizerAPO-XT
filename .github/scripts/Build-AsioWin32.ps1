<#
.SYNOPSIS
    Builds the 32-bit ASIO wrapper DLL and the 32-bit fake driver.

.DESCRIPTION
    A 32-bit DAW can only load a 32-bit ASIO driver, so the wrapper DLL ships
    for Win32 as well; the engine stays in the 64-bit host process. Neither
    DLL links the engine or takes a SIMD flag, so one build on the avx2 leg
    covers every variant. The fake driver is built alongside so a future
    32-bit probe leg has its target.

    -PlanOnly returns the projects and the MSBuild arguments, for Pester.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [string] $Configuration = "Release",
    [string] $PlatformToolset = "v145",
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"

$projects = @(
    "EqualizerAPOAsio\EqualizerAPOAsio.vcxproj",
    "Tests\FakeAsioDriver\FakeAsioDriver.vcxproj"
)
$buildParams = @(
    "/m", "/p:Configuration=$Configuration", "/p:Platform=Win32", "/t:rebuild",
    "/p:PlatformToolset=$PlatformToolset", "/p:PreferredToolArchitecture=x64",
    "/p:ASIO_SDK=$env:ASIO_SDK"
)
$plan = [pscustomobject]@{
    Projects = $projects
    BuildParams = $buildParams
    Outputs = @(
        (Join-Path $WorkspaceRoot "EqualizerAPOAsio\$Configuration\EqualizerAPOAsio.dll"),
        (Join-Path $WorkspaceRoot "Tests\FakeAsioDriver\$Configuration\FakeAsioDriver.dll")
    )
}
if ($PlanOnly) { return $plan }

Set-Location $WorkspaceRoot
foreach ($project in $projects) {
    msbuild $project @buildParams
    if ($LASTEXITCODE -ne 0) { throw "Win32 build failed for $project" }
}
foreach ($output in $plan.Outputs) {
    if (-not (Test-Path -LiteralPath $output)) { throw "Win32 build produced no $output" }
}
Write-Host "32-bit ASIO wrapper and fake driver built"
