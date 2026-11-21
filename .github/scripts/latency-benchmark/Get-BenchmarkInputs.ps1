<#
.SYNOPSIS
    Fetches the benchmark's private inputs onto the runner and checks them.

.DESCRIPTION
    The inputs are not redistributable, so they live in a private repository
    that a read-only deploy key opens. The key comes from the environment
    variable BENCH_ASSETS_DEPLOY_KEY and is written to a file only this script
    uses; the file is removed before the script returns. The connection is
    accepted only from the host key GitHub publishes. Every input is checked
    against the repository's SHA256SUMS and the hashes pinned below, so a
    changed input cannot change a published number unnoticed.
#>
param(
    [Parameter(Mandatory)] [string] $Destination,
    [string] $Repository = "git@github.com:115dkk/eapo-bench-assets-tmp.git"
)
$ErrorActionPreference = "Stop"

# https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints
$githubHostKey = "github.com ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl"

$pinned = @{
    "MultiChannelPluginHost-1.1.1-Windows-x64-Setup.exe" = "2962df7b214ce1fc546f9b88673c9a6d9a651e4135943226a140e6575eaee5cc"
}

if ([string]::IsNullOrWhiteSpace($env:BENCH_ASSETS_DEPLOY_KEY)) {
    throw "BENCH_ASSETS_DEPLOY_KEY is not set. The benchmark's inputs are private; without the key this workflow cannot run."
}

$keyDirectory = Join-Path ([IO.Path]::GetTempPath()) ("bench-key-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $keyDirectory | Out-Null
$keyFile = Join-Path $keyDirectory "id"
$knownHosts = Join-Path $keyDirectory "known_hosts"
try {
    # OpenSSH refuses a key without a trailing newline or with CRLF.
    $key = ($env:BENCH_ASSETS_DEPLOY_KEY -replace "`r", "").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText($keyFile, $key, [Text.UTF8Encoding]::new($false))
    & icacls $keyFile /inheritance:r /grant:r "$($env:USERNAME):R" | Out-Null
    [IO.File]::WriteAllText($knownHosts, $githubHostKey + "`n", [Text.UTF8Encoding]::new($false))

    if (Test-Path $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
    $sshKey = $keyFile -replace "\\", "/"
    $sshHosts = $knownHosts -replace "\\", "/"
    $env:GIT_SSH_COMMAND = "ssh -i `"$sshKey`" -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=`"$sshHosts`""
    & git clone --quiet --depth 1 $Repository $Destination
    if ($LASTEXITCODE -ne 0) { throw "could not fetch the private inputs (git exit $LASTEXITCODE)" }
}
finally {
    Remove-Item Env:\GIT_SSH_COMMAND -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $keyDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
Remove-Item -LiteralPath (Join-Path $Destination ".git") -Recurse -Force

# The repository's own list first (every file accounted for), then the pins.
$listed = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $Destination "SHA256SUMS")) {
    if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+)$') { $listed[$Matches[2].Trim()] = $Matches[1].ToLowerInvariant() }
}
foreach ($file in Get-ChildItem -LiteralPath $Destination -File | Where-Object { $_.Name -notin "SHA256SUMS", ".gitattributes" }) {
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if (-not $listed.ContainsKey($file.Name)) { throw "input $($file.Name) is not in SHA256SUMS" }
    if ($listed[$file.Name] -ne $hash) { throw "input $($file.Name) does not match SHA256SUMS" }
    if ($pinned.ContainsKey($file.Name) -and $pinned[$file.Name] -ne $hash) { throw "input $($file.Name) does not match the hash pinned in this script" }
    Write-Host ("input {0}  {1} bytes  sha256 {2}" -f $file.Name, $file.Length, $hash)
}
