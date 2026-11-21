<#
.SYNOPSIS
    Runs the latency benchmark only on a disposable GitHub Windows runner.
    Private inputs, configurations and all audio remain under Inputs or Work.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)] [string] $Inputs,
    [Parameter(Mandatory)] [string] $Work,
    [Parameter(Mandatory)] [string] $Summary
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows') {
    throw 'This script may run only on a disposable GitHub Windows runner.'
}
foreach ($directory in @($Work, $Summary)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
foreach ($name in @('zAuro-0.4.1-Windows-x64-Setup.exe', 'MultiChannelPluginHost-1.1.1-Windows-x64-Setup.exe', 'brir22.wav', 'bp_MUS_CosmosBATTLE.ogg', 'config-714.txt', 'SHA256SUMS')) {
    if (-not (Test-Path -LiteralPath (Join-Path $Inputs $name))) { throw 'A required private input is missing.' }
}

function Invoke-Bounded([string] $Program, [string[]] $Arguments, [int] $Seconds, [string] $Label) {
    $info = [Diagnostics.ProcessStartInfo]::new($Program)
    $info.UseShellExecute = $false
    $info.WorkingDirectory = $WorkspaceRoot
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        [void]$process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($Seconds * 1000)) {
            $process.Kill($true)
            [void]$process.WaitForExit(10000)
            throw "$Label timed out."
        }
        [IO.File]::WriteAllText((Join-Path $Work "$Label.stdout.log"), $stdout.GetAwaiter().GetResult())
        [IO.File]::WriteAllText((Join-Path $Work "$Label.stderr.log"), $stderr.GetAwaiter().GetResult())
        if ($process.ExitCode -ne 0) { throw "$Label exited with code $($process.ExitCode). Logs remain in Work." }
    }
    finally {
        if ($process.Id -and -not $process.HasExited) { $process.Kill($true) }
        $process.Dispose()
    }
}

# Download wheels without executing them; the first run publishes only their
# names and hashes. Subsequent runs must match the checked-in complete lock.
$packages = @('numpy==2.3.5', 'scipy==1.16.3', 'soundfile==0.13.1', 'PyYAML==6.0.3', 'psutil==7.1.3', 'cffi==2.0.0', 'pycparser==2.23', 'netCDF4==1.7.3', 'cftime==1.6.5', 'certifi==2025.11.12')
$wheels = Join-Path $Work 'wheels'
New-Item -ItemType Directory -Path $wheels | Out-Null
Invoke-Bounded 'python' (@('-I', '-m', 'pip', 'download', '--disable-pip-version-check', '--only-binary=:all:', '--no-deps', '--dest', $wheels) + $packages) 300 'wheel-download'
$actual = [ordered]@{}
foreach ($file in Get-ChildItem -LiteralPath $wheels -File) {
    $actual[$file.Name] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$actual | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Summary 'wheel-hashes.json')
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'wheel-hashes.json') -Raw | ConvertFrom-Json -AsHashtable
$discovery = $lock.Count -eq 0
if ($discovery) {
    $actual | ConvertTo-Json | Write-Host
    @{ status = 'not measured'; reason = 'Dependency hash discovery; pin wheel-hashes.json before executing downloaded wheels.' } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Summary 'results.json')
    'Not measured: dependency hash discovery. No downloaded wheel was executed.' | Set-Content -LiteralPath (Join-Path $Summary 'results.md')
    exit 0
}
if ($actual.Count -ne $lock.Count) { throw 'Wheel set does not match the lock.' }
foreach ($name in $actual.Keys) {
    if (-not $lock.ContainsKey($name) -or $lock[$name] -ne $actual[$name]) { throw 'A downloaded wheel does not match the pinned SHA-256.' }
}
$requirements = @($actual.Keys | ForEach-Object { "$(Join-Path $wheels $_) --hash=sha256:$($actual[$_])" })
$requirements | Set-Content -LiteralPath (Join-Path $Work 'requirements.txt')
Invoke-Bounded 'python' @('-I', '-m', 'pip', 'install', '--disable-pip-version-check', '--no-index', '--no-deps', '--require-hashes', '-r', (Join-Path $Work 'requirements.txt')) 180 'wheel-install'
$cpu = Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors
$machine = @{ cpu = @($cpu); memory_bytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory; runner = $env:ImageVersion; run_id = $env:GITHUB_RUN_ID; commit = $env:GITHUB_SHA }
$machine | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Work 'machine.json')
Invoke-Bounded 'python' @('-I', (Join-Path $PSScriptRoot 'benchmark.py'), '--workspace', $WorkspaceRoot, '--inputs', $Inputs, '--work', $Work, '--summary', $Summary) 4800 'benchmark'
Write-Host 'Benchmark finished. Only measured numbers and explicit blockers are in the summary.'
exit 0
