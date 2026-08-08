<#
.SYNOPSIS
    Resolves the dependency include/lib paths under deps\ and exports them as
    the *_INCLUDE / *_LIB / *_ROOT environment the vcxproj defaults consume.

.DESCRIPTION
    Extracted from the inline "Setup dependency structures" step in build.yml
    (audit #250 F068): the step re-stated deps layout knowledge that
    Directory.Build.props and setup-build.ps1 also carry, with no way to test
    the probe-or-fail decisions outside a live runner.

    The plan phase probes the layout (fftw3.h and libfftw3.lib are searched
    for because the extracted FFTW archive nests them differently per
    variant; the rest are fixed spellings off the deps root) and returns the
    ordered name -> path map, throwing on the four fatal absences. Execution
    appends the map to $env:GITHUB_ENV and prints the structure listings the
    CI log always carried.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $DepsPath,
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"

$fftwHeader = Get-ChildItem -Path "$DepsPath\fftw\Include" -Recurse -Filter "fftw3.h" -ErrorAction SilentlyContinue | Select-Object -First 1
$fftwLib = Get-ChildItem -Path "$DepsPath\fftw\Release" -Recurse -Filter "libfftw3.lib" -ErrorAction SilentlyContinue | Select-Object -First 1
$mpxLib = Get-ChildItem -Path "$DepsPath\muparserx\build\Release" -Recurse -Filter "muparserx.lib" -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $fftwHeader) { throw "fftw3.h not found under $DepsPath\fftw\Include" }
if (-not $fftwLib) { throw "libfftw3.lib not found under $DepsPath\fftw\Release" }
if (-not (Test-Path "$DepsPath\muparserx")) { throw "muparserx folder not found under $DepsPath" }
if (-not $mpxLib) { throw "muparserx.lib not found under $DepsPath\muparserx\build\Release" }

$environment = [ordered]@{
    FFTW_INCLUDE       = $fftwHeader.Directory.FullName
    FFTW_LIB           = $fftwLib.Directory.FullName
    MUPARSERX_INCLUDE  = "$DepsPath\muparserx\parser"
    MUPARSERX_LIB      = $mpxLib.Directory.FullName
    LIBSNDFILE_INCLUDE = "$DepsPath\libsndfile\include"
    LIBSNDFILE_LIB     = "$DepsPath\libsndfile\build\Release"
    TCLAP_ROOT         = "$DepsPath\tclap"
    # VST3 SDK root (contains pluginterfaces/) for VST3 hosting headers.
    VST3_SDK           = "$DepsPath\vst3sdk"
    # Highway header root (contains hwy/) for the portable SIMD kernels.
    HIGHWAY_INCLUDE    = "$DepsPath\highway"
    # Velopack runtime paths for the Editor's auto-update link.
    VELOPACK_INCLUDE   = "$DepsPath\velopack_libc\include"
    VELOPACK_LIB       = "$DepsPath\velopack_libc\lib"
}

if ($PlanOnly) {
    return [pscustomobject]$environment
}

Write-Host "=== Dependency structures ==="
foreach ($dependency in @(
        @{ Name = "FFTW"; Path = "$DepsPath\fftw"; Depth = $null },
        @{ Name = "muParserX"; Path = "$DepsPath\muparserx"; Depth = $null },
        @{ Name = "libsndfile"; Path = "$DepsPath\libsndfile"; Depth = $null },
        @{ Name = "TCLAP"; Path = "$DepsPath\tclap"; Depth = 1 }))
{
    Write-Host "`n$($dependency.Name) structure:"
    if (Test-Path $dependency.Path) {
        if ($null -ne $dependency.Depth) {
            Get-ChildItem $dependency.Path -Recurse -Depth $dependency.Depth | Select-Object FullName
        } else {
            Get-ChildItem $dependency.Path -Recurse | Select-Object FullName
        }
    } else {
        Write-Warning "$($dependency.Name) directory not found"
    }
}

foreach ($pair in $environment.GetEnumerator()) {
    "$($pair.Key)=$($pair.Value)" >> $env:GITHUB_ENV
    Write-Host "$($pair.Key)=$($pair.Value)"
}
Write-Host "`nAll dependency paths configured successfully"
