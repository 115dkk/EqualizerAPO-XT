<#
.SYNOPSIS
    Builds the unchanged reference-bridge crates, with an explicit local mode.
    Local mode downloads only the pinned source, never installs Omniphony.
#>
[CmdletBinding()]
param(
    [string] $Source,
    [Parameter(Mandatory)] [string] $Work,
    [switch] $Local
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Local) {
    if ($env:GITHUB_ACTIONS -eq 'true') { throw 'Local mode is forbidden on a runner.' }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if ([Security.Principal.WindowsPrincipal]::new($identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Elevated execution is forbidden.'
    }
    if ($Source) { throw 'Local mode prepares its own pinned source; do not supply -Source.' }
}
elseif ($env:GITHUB_ACTIONS -ne 'true' -or -not $Source) {
    throw 'Use explicit -Local for a local bridge build, or supply the runner workspace.'
}
$Work = [IO.Path]::GetFullPath($Work)
if ($Local) {
    for ($directory = [IO.DirectoryInfo]::new($Work); $null -ne $directory; $directory = $directory.Parent) {
        if (Test-Path -LiteralPath (Join-Path $directory.FullName '.git')) { throw 'Local bridge outputs must not be in a git worktree.' }
    }
    foreach ($protected in @((Join-Path $env:LOCALAPPDATA 'EqualizerAPO-XT'), (Join-Path $env:LOCALAPPDATA 'EqualizerAPO-XT-x64-avx2'))) {
        $a = $Work.TrimEnd('\') + '\'
        $b = [IO.Path]::GetFullPath($protected).TrimEnd('\') + '\'
        if ($a.StartsWith($b, [StringComparison]::OrdinalIgnoreCase) -or $b.StartsWith($a, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Local bridge outputs overlap the installation or live config.'
        }
    }
}
New-Item -ItemType Directory -Path $Work -Force | Out-Null
$logs = Join-Path $Work 'bridge-build-logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null

function Invoke-BridgeCommand([string] $Executable, [string[]] $Arguments, [int] $Seconds, [string] $Label, [switch] $CargoEnvironment) {
    $info = [Diagnostics.ProcessStartInfo]::new($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WorkingDirectory = $Work
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    if ($Local -and $Executable -eq 'git') {
        $hooks = Join-Path $Work ('empty-hooks-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $hooks | Out-Null
        foreach ($option in @('-c', 'credential.helper=', '-c', 'core.askPass=', '-c', 'core.fsmonitor=false', '-c', "core.hooksPath=$hooks")) { $info.ArgumentList.Add($option) }
    }
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $info.Environment.Remove('GH_TOKEN') | Out-Null
    if ($Local) {
        $temporary = Join-Path $Work 'bridge-temp'
        New-Item -ItemType Directory -Path $temporary -Force | Out-Null
        $info.Environment['TEMP'] = $temporary
        $info.Environment['TMP'] = $temporary
        $info.Environment['GIT_TERMINAL_PROMPT'] = '0'
        $info.Environment['GCM_INTERACTIVE'] = 'Never'
        $info.Environment['GIT_LFS_SKIP_SMUDGE'] = '1'
        $info.Environment.Remove('GIT_ASKPASS') | Out-Null
        $info.Environment.Remove('SSH_ASKPASS') | Out-Null
    }
    if ($CargoEnvironment) {
        $info.Environment['CARGO_NET_GIT_FETCH_WITH_CLI'] = 'true'
        $info.Environment['GIT_CONFIG_COUNT'] = '1'
        $info.Environment['GIT_CONFIG_KEY_0'] = 'protocol.https.allow'
        $info.Environment['GIT_CONFIG_VALUE_0'] = 'never'
        $info.Environment['CARGO_TARGET_DIR'] = Join-Path $Work 'omni-target'
        if ($Local) {
            $info.Environment['CARGO_HOME'] = Join-Path $Work 'cargo-home'
            $info.Environment['CARGO_BUILD_JOBS'] = '4'
            $info.Environment['RUSTUP_AUTO_INSTALL'] = '0'
        }
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    $started = $false
    try {
        $started = $process.Start()
        if ($Local) {
            try { $process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal }
            catch { if (-not $process.HasExited) { throw } }
        }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($Seconds * 1000)) {
            $process.Kill($true)
            [void]$process.WaitForExit(10000)
            throw 'An owned bridge-build process exceeded its deadline.'
        }
        [IO.File]::WriteAllText((Join-Path $logs "$Label.stdout.log"), $stdout.GetAwaiter().GetResult())
        [IO.File]::WriteAllText((Join-Path $logs "$Label.stderr.log"), $stderr.GetAwaiter().GetResult())
        if ($process.ExitCode -ne 0) { throw "Bridge-build command exit code $($process.ExitCode)." }
    }
    finally {
        if ($started -and -not $process.HasExited) { $process.Kill($true); [void]$process.WaitForExit(10000) }
        $process.Dispose()
    }
}

if ($Local) {
    $checkout = Join-Path $Work 'omni-source'
    $archive = Join-Path $Work 'omni-source.tar'
    $Source = Join-Path $Work 'omni-bridge-build'
    if ((Test-Path -LiteralPath $checkout) -or (Test-Path -LiteralPath $Source) -or (Test-Path -LiteralPath $archive)) {
        throw 'Use a new empty bridge work directory; existing source/build files will not be overwritten.'
    }
    Invoke-BridgeCommand 'git' @('clone', '--depth', '1', '--branch', 'v0.7.0', 'https://github.com/mgth/Omniphony.git', $checkout) 300 'source'
    Invoke-BridgeCommand 'git' @('-C', $checkout, 'rev-parse', 'HEAD') 30 'revision'
    $revision = ([IO.File]::ReadAllText((Join-Path $logs 'revision.stdout.log'))).Trim()
    if ($revision -ne '353e135fa01e181bea36d83099a5951fd5c34180') { throw 'The source commit differs from the pin.' }
    Invoke-BridgeCommand 'git' @('-C', $checkout, 'archive', '--format=tar', '--output', $archive, 'HEAD') 90 'archive'
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne 'ad2012377fbd0760794347d2b900f5a6508ab386d9fff22bb4e9a3e2c17fddbb') {
        throw 'The source SHA-256 differs from the pin; no downloaded source was built.'
    }
    Invoke-BridgeCommand 'python' @('-I', (Join-Path $PSScriptRoot 'local_bench.py'), '_bridge-workspace', '--work', $Work, '--source', $checkout) 60 'isolate'
}
Invoke-BridgeCommand 'cargo' @('build', '--manifest-path', (Join-Path $Source 'Cargo.toml'), '--release', '--locked', '-p', 'reference_bridge') 1500 'cargo' -CargoEnvironment
$bridge = Join-Path $Work 'omni-target\release\reference_bridge.dll'
if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) { throw 'The expected bridge DLL was not produced.' }
@{ exit_code = 0; bridge_exists = 1; local_mode = [int]$Local.IsPresent } | ConvertTo-Json -Compress | Write-Output
exit 0
