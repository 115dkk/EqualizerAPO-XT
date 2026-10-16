[CmdletBinding()]
param(
    [string] $WorkspaceRoot = (Join-Path $PSScriptRoot ".." ".."),
    [string] $DepsPath,
    [switch] $PlanOnly
)

# The C++ build CodeQL traces (.github/workflows/codeql.yml). The extractor
# hooks cl.exe, so it needs the compiles, not the links: Common is built
# properly (a static library, cheap) and everything else compiles only
# (/t:ClCompile), which also sidesteps the ATL component the standard runner
# lacks (atls.lib).
#
# Every C++ binary the release ships is here, and BuildScripts.Tests.ps1
# holds this list to Package-Artifacts.ps1's shipped files: until audit #348
# TD-25 the list was four projects written into the YAML, and the ASIO
# wrapper a DAW loads, the engine host behind it, the Subwoofer Routing VST3
# and the installer that runs from a Downloads folder were outside the scan.
# The Qt apps are not here: they build through qmake against Qt, which the
# CodeQL job does not install.
$targets = @(
    [pscustomobject]@{ Project = "Common.vcxproj"; Target = "Build"; Platform = "x64" },
    [pscustomobject]@{ Project = "EqualizerAPO\EqualizerAPO.vcxproj"; Target = "ClCompile"; Platform = "x64" },
    [pscustomobject]@{ Project = "VoicemeeterClient\VoicemeeterClient.vcxproj"; Target = "ClCompile"; Platform = "x64" },
    [pscustomobject]@{ Project = "Benchmark\Benchmark.vcxproj"; Target = "ClCompile"; Platform = "x64" },
    [pscustomobject]@{ Project = "EqualizerAPOAsio\EqualizerAPOAsio.vcxproj"; Target = "ClCompile"; Platform = "x64" },
    [pscustomobject]@{ Project = "EqualizerAPOHost\EqualizerAPOHost.vcxproj"; Target = "ClCompile"; Platform = "x64" },
    [pscustomobject]@{ Project = "VST3\SubwooferRouting\SubwooferRoutingVst3.vcxproj"; Target = "ClCompile"; Platform = "x64" },
    # The x86 wrapper 32-bit DAWs load, shipped under x86\ on the x64 legs.
    [pscustomobject]@{ Project = "EqualizerAPOAsio\EqualizerAPOAsio.vcxproj"; Target = "ClCompile"; Platform = "Win32" },
    # The auto-detect installer: Win32-only, a separate release asset.
    [pscustomobject]@{ Project = "Installer\Installer.vcxproj"; Target = "ClCompile"; Platform = "Win32" }
)
if ($PlanOnly) { return [pscustomobject]@{ Targets = $targets } }

$ErrorActionPreference = "Stop"
if (-not $DepsPath) { $DepsPath = Join-Path $WorkspaceRoot "deps" }
Set-Location $WorkspaceRoot

# Header paths only: nothing links, so no library paths. v145, the projects'
# default: codeql.yml runs on the windows-2025-vs2026 image, whose VS 2026
# carries it (audit #348 TD-78; the v143 pin dated from a VS 2022 image).
$env:FFTW_INCLUDE = "$DepsPath\fftw\include"
$env:MUPARSERX_INCLUDE = "$DepsPath\muparserx\parser"
$env:LIBSNDFILE_INCLUDE = "$DepsPath\libsndfile\include"
$env:TCLAP_ROOT = "$DepsPath\tclap"
$env:VST3_SDK = "$DepsPath\vst3sdk"
$env:HIGHWAY_INCLUDE = "$DepsPath\highway"
$env:ASIO_SDK = "$DepsPath\asiosdk\ASIOSDK"
Import-Module (Join-Path $PSScriptRoot "BuildModel.psm1") -Force
foreach ($target in $targets) {
    # The avx2 code paths are the ones most users run.
    $params = Get-EapoMsBuildArguments -Platform $target.Platform -Target $target.Target `
        -ArchFlag AdvancedVectorExtensions2 -Dependencies Includes -Only @(
            "FFTW_INCLUDE", "MUPARSERX_INCLUDE", "LIBSNDFILE_INCLUDE", "TCLAP_ROOT",
            "VST3_SDK", "HIGHWAY_INCLUDE", "ASIO_SDK")
    Write-Host "`n=== $($target.Target) $($target.Project) ($($target.Platform)) ==="
    Invoke-EapoMsBuild -Project $target.Project -Arguments $params -FailureLabel "CodeQL build ($($target.Platform))"
}
