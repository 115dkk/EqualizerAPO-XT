<#
.SYNOPSIS
    Runs the ASIO probe gate: the wrapped stream must hash exactly like the
    engine's direct output, first period included.

.DESCRIPTION
    Two shapes, both over Tests/AsioProbe/probe-config.txt with the fake
    driver stepped deterministically (docs/architecture/asio-host-study.md,
    section 10.3):

      inproc   AsioWrapper linked into the probe with the in-process engine
               adapter; output, input and first-period hashes must equal the
               probe's own direct engine run, and no block may be late.
      dll      EqualizerAPOAsio.dll over FakeAsioDriver.dll through the DLLs'
               own entry points (DllGetClassObject, EapoAsioCreateWrapper) in
               passthrough, which must be a byte-for-byte identity.

    -PlanOnly returns the runs without executing anything, for Pester.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [ValidateSet("x64", "ARM64")] [string] $Platform = "x64",
    [string] $Configuration = "Release",
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"

$probe = Join-Path $WorkspaceRoot "Tests\AsioProbe\$Platform\$Configuration\AsioProbe.exe"
$fakeDll = Join-Path $WorkspaceRoot "Tests\FakeAsioDriver\$Platform\$Configuration\FakeAsioDriver.dll"
$wrapperDll = Join-Path $WorkspaceRoot "EqualizerAPOAsio\$Platform\$Configuration\EqualizerAPOAsio.dll"
$config = Join-Path $WorkspaceRoot "Tests\AsioProbe\probe-config.txt"

$runs = @(
    [pscustomobject]@{
        Name = "inproc-int32-64"
        Arguments = @("--target", "fake", "--wrapper", "static", "--processor", "inproc", "--config", $config,
            "--frames", "64", "--periods", "300", "--sample-type", "int32", "--max-late", "0")
    },
    [pscustomobject]@{
        Name = "inproc-int24-128-outputready"
        Arguments = @("--target", "fake", "--wrapper", "static", "--processor", "inproc", "--config", $config,
            "--frames", "128", "--periods", "150", "--sample-type", "int24", "--output-ready", "--max-late", "0")
    },
    [pscustomobject]@{
        Name = "inproc-float32-32-output-only"
        Arguments = @("--target", "fake", "--wrapper", "static", "--processor", "inproc", "--config", $config,
            "--frames", "32", "--periods", "400", "--sample-type", "float32", "--no-input", "--max-late", "0")
    },
    [pscustomobject]@{
        Name = "dll-passthrough-int24-128"
        Arguments = @("--target", "dll:$fakeDll", "--wrapper", "dll:$wrapperDll", "--processor", "passthrough",
            "--config", $config, "--frames", "128", "--periods", "100", "--sample-type", "int24")
    }
)

$plan = [pscustomobject]@{
    Probe = $probe
    FakeDriver = $fakeDll
    WrapperDll = $wrapperDll
    Config = $config
    Runs = $runs
}
if ($PlanOnly) { return $plan }

foreach ($required in @($probe, $fakeDll, $wrapperDll, $config)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "ASIO probe gate: $required is missing" }
}

$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$env:MUPARSERX_LIB;$env:PATH"
foreach ($run in $runs) {
    Write-Host "=== ASIO probe: $($run.Name) ==="
    & $probe @($run.Arguments)
    if ($LASTEXITCODE -ne 0) { throw "ASIO probe gate: $($run.Name) failed with exit code $LASTEXITCODE" }
}
Write-Host "ASIO probe gate: $($runs.Count) runs passed"
