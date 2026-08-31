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
$hostExe = Join-Path $WorkspaceRoot "EqualizerAPOHost\$Platform\$Configuration\EqualizerAPOHost.exe"

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
    # The daemon adapter over the engine host on a thread: the ring and the
    # serving loop without process-spawn noise, sync and pipelined.
    [pscustomobject]@{
        Name = "daemon-thread-sync-int32-64"
        Arguments = @("--target", "fake", "--wrapper", "static", "--processor", "daemon-thread", "--config", $config,
            "--frames", "64", "--periods", "300", "--sample-type", "int32", "--deadline-us", "1000000", "--max-late", "0")
    },
    [pscustomobject]@{
        Name = "daemon-thread-pipelined-int24-128"
        Arguments = @("--target", "fake", "--wrapper", "static", "--processor", "daemon-thread", "--config", $config,
            "--frames", "128", "--periods", "150", "--sample-type", "int24", "--mode", "pipelined", "--max-late", "0")
    },
    # The real host process, started by the link on the probe's own
    # endpoint. A one-second sync deadline keeps a shared runner's scheduling
    # out of the hash; the timing run is a separate, informational matter.
    [pscustomobject]@{
        Name = "daemon-exe-sync-int32-64"
        Arguments = @("--target", "fake", "--wrapper", "static", "--processor", "daemon", "--config", $config,
            "--daemon", $hostExe, "--endpoint", "EAPO.ASIO.probe.gate", "--frames", "64", "--periods", "300",
            "--sample-type", "int32", "--deadline-us", "1000000", "--max-late", "0")
    },
    # The one run with the real host process AND the pipelined mode, which by
    # design lets a late period through unprocessed. On a hosted runner under
    # load that turned the hash comparison into a coin toss (two failures on
    # 2026-08-30, both "first period is not the engine's first period", both
    # green on rerun with nothing changed). The run keeps its assertion and
    # gets three attempts: a genuine regression fails all three.
    # Since the pipelined callback stopped waiting in the kernel (zero-wait,
    # 2026-08-31), a back-to-back pump would make every period late by
    # construction; --pace-us restores the between-period wall time real
    # hardware provides. The retries stay for scheduler stalls.
    [pscustomobject]@{
        Name = "dll-daemon-exe-pipelined-float32-32"
        Arguments = @("--target", "dll:$fakeDll", "--wrapper", "dll:$wrapperDll", "--processor", "daemon", "--config", $config,
            "--daemon", $hostExe, "--endpoint", "EAPO.ASIO.probe.gate", "--frames", "32", "--periods", "400",
            "--sample-type", "float32", "--mode", "pipelined", "--pace-us", "2000")
        Attempts = 3
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
    HostExe = $hostExe
    Config = $config
    Runs = $runs
}
if ($PlanOnly) { return $plan }

foreach ($required in @($probe, $fakeDll, $wrapperDll, $hostExe, $config)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "ASIO probe gate: $required is missing" }
}

$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$env:MUPARSERX_LIB;$env:PATH"
foreach ($run in $runs) {
    $attempts = if ($run.PSObject.Properties["Attempts"]) { [int]$run.Attempts } else { 1 }
    for ($attempt = 1; $attempt -le $attempts; $attempt++) {
        Write-Host "=== ASIO probe: $($run.Name)$(if ($attempts -gt 1) { " (attempt $attempt of $attempts)" }) ==="
        & $probe @($run.Arguments)
        if ($LASTEXITCODE -eq 0) { break }
        if ($attempt -eq $attempts) { throw "ASIO probe gate: $($run.Name) failed with exit code $LASTEXITCODE" }
        Write-Host "ASIO probe gate: $($run.Name) exited with $LASTEXITCODE; a timing-bound run, trying again"
        Start-Sleep -Seconds 5
    }
}
Write-Host "ASIO probe gate: $($runs.Count) runs passed"
