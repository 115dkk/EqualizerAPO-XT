<#
.SYNOPSIS
    Builds the unchanged reference-bridge crates in an isolated workspace.
    Cargo verifies registry packages against the upstream SHA-256 checksums.
#>
param(
    [Parameter(Mandatory)] [string] $Source,
    [Parameter(Mandatory)] [string] $Work
)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Runner only.' }
# Reject unexpected git dependencies instead of expanding the network policy.
$env:CARGO_NET_GIT_FETCH_WITH_CLI = 'true'
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'protocol.https.allow'
$env:GIT_CONFIG_VALUE_0 = 'never'
$env:CARGO_TARGET_DIR = Join-Path $Work 'omni-target'
& cargo build --manifest-path (Join-Path $Source 'Cargo.toml') --release --locked -p reference_bridge
exit $LASTEXITCODE
