<#
.SYNOPSIS
    Removes exactly one Device section, or restores the approved backup bytes.
    This script is never invoked by the workflow or by local_bench.py.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Config,
    [Parameter(Mandatory)] [string] $Backup,
    [Parameter(Mandatory)] [ValidateSet('Remove', 'Restore')] [string] $Mode,
    [string] $DeviceMatch = 'CABLE In 16ch'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$expected = '49dfbc4d9a2fa664d6135d0b07de2dbbd1a626b86ff1c88b0da20a985110f5ff'
if ($env:GITHUB_ACTIONS -eq 'true') { throw 'Local config swap is forbidden on a runner.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if ([Security.Principal.WindowsPrincipal]::new($identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Elevated execution is forbidden.'
}
$Config = [IO.Path]::GetFullPath($Config)
$Backup = [IO.Path]::GetFullPath($Backup)
$approvedConfig = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'EqualizerAPO-XT\config\config.txt'))
if ($Config -ine $approvedConfig -or $Backup -ieq $Config -or $DeviceMatch -cne 'CABLE In 16ch') {
    throw 'The requested target is outside the approved config swap.'
}
if ([IO.Path]::GetDirectoryName($Backup) -ieq [IO.Path]::GetDirectoryName($Config)) {
    throw 'Use the backup under localbench, not a backup beside the live config.'
}
$backupDirectory = [IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($Backup))
for ($directory = $backupDirectory; $null -ne $directory; $directory = $directory.Parent) {
    if (Test-Path -LiteralPath (Join-Path $directory.FullName '.git')) { throw 'Backup/marker output must not be in a git worktree.' }
}
if (-not (Test-Path -LiteralPath $Backup -PathType Leaf)) { throw 'The original backup is missing.' }
foreach ($path in @($Config, $Backup)) {
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $item = Get-Item -LiteralPath $path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            ($item.PSObject.Properties['LinkType'] -and $item.LinkType)) {
            throw 'Use ordinary files, not symbolic links or hard links, for this config swap.'
        }
    }
}
$original = [IO.File]::ReadAllBytes($Backup)
function Get-BytesHash([byte[]] $Bytes) {
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}
$backupHash = Get-BytesHash $original
if ($backupHash -ne $expected) { throw 'The backup does not match the approved original SHA-256.' }
$marker = $Backup + '.swap.json'

if ($Mode -eq 'Remove') {
    if (-not (Test-Path -LiteralPath $Config -PathType Leaf)) { throw 'The live config is missing.' }
    $current = [IO.File]::ReadAllBytes($Config)
    if ((Get-BytesHash $current) -ne $backupHash) { throw 'The live config differs from the original backup; nothing was changed.' }
    # Identify line boundaries in raw bytes. Only matching uses decoded text;
    # no line is re-encoded and no other Device section is rewritten.
    $bom = if ($original.Length -ge 3 -and $original[0] -eq 239 -and $original[1] -eq 187 -and $original[2] -eq 191) { 3 } else { 0 }
    $headers = [Collections.Generic.List[object]]::new()
    $conditionalDepth = 0
    $start = $bom
    while ($start -lt $original.Length) {
        $end = $start
        while ($end -lt $original.Length -and $original[$end] -ne 10) { $end++ }
        if ($end -lt $original.Length) { $end++ }
        $line = [Text.Encoding]::UTF8.GetString($original, $start, $end - $start)
        if ($line -match '^[\t ]*If[\t ]*:') { $conditionalDepth++ }
        if ($line -match '^[\t ]*EndIf[\t ]*:') { $conditionalDepth-- }
        if ($conditionalDepth -lt 0) { throw 'Conditional boundaries are not balanced; nothing was changed.' }
        if ($line -match '^[\t ]*Device[\t ]*:(.*)$') {
            $selector = ($Matches[1] -split '#', 2)[0]
            $headers.Add([pscustomobject]@{ Start = $start; Depth = $conditionalDepth; Match = $selector.Contains($DeviceMatch, [StringComparison]::Ordinal); Ambiguous = ($selector -match '(?i)TOPPING|Realphones|Steam|NVIDIA') -or $selector.Contains('!') })
        }
        $start = $end
    }
    $selectedHeaders = @($headers | Where-Object Match)
    if ($selectedHeaders.Count -ne 1) { throw 'The requested Device section was not found exactly once; nothing was changed.' }
    if ($selectedHeaders[0].Ambiguous) { throw 'The matching selector is negated or includes another protected device; nothing was changed.' }
    if ($selectedHeaders[0].Depth -ne 0 -or $conditionalDepth -ne 0) { throw 'The selected section crosses a conditional boundary; nothing was changed.' }
    $removeStart = [int]$selectedHeaders[0].Start
    $removeEnd = $original.Length
    foreach ($header in $headers) {
        if ($header.Start -gt $removeStart) {
            if ($header.Depth -ne 0) { throw 'The selected section crosses a conditional boundary; nothing was changed.' }
            $removeEnd = [int]$header.Start
            break
        }
    }
    $replacement = [byte[]]::new($original.Length - ($removeEnd - $removeStart))
    [Array]::Copy($original, 0, $replacement, 0, $removeStart)
    [Array]::Copy($original, $removeEnd, $replacement, $removeStart, $original.Length - $removeEnd)
    $replacementHash = Get-BytesHash $replacement
    $transaction = [Guid]::NewGuid().ToString('N')
    # Invalidate any older success marker before touching the live file.
    [ordered]@{ removed = 1; backup_sha256 = $backupHash; in_progress = 1 } |
        ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8
    try {
        # Write only this file; do not create a temp file in the live config dir.
        [IO.File]::WriteAllBytes($Config, $replacement)
        Start-Sleep -Seconds 3
        $writtenHash = (Get-FileHash -LiteralPath $Config -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($writtenHash -ne $replacementHash) { throw 'The temporary config hash did not match.' }
        [ordered]@{ removed = 1; backup_sha256 = $backupHash; temporary_sha256 = $writtenHash; transaction = $transaction; removed_bytes = $removeEnd - $removeStart } |
            ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8
        [ordered]@{ removed = 1; backup_sha256 = $backupHash; config_sha256 = $writtenHash; write_verified = 1; removed_bytes = $removeEnd - $removeStart } |
            ConvertTo-Json -Compress | Write-Output
    }
    catch {
        [IO.File]::WriteAllBytes($Config, $original)
        Start-Sleep -Seconds 3
        $restored = (Get-FileHash -LiteralPath $Config -Algorithm SHA256).Hash.ToLowerInvariant()
        [ordered]@{ removed = 0; backup_sha256 = $backupHash; config_sha256 = $restored; equal = [int]($restored -eq $backupHash) } |
            ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8
        throw 'Remove failed; restoration was attempted. Run Restore and check its hashes before continuing.'
    }
}
else {
    $restored = $null
    $equal = $false
    try {
        [IO.File]::WriteAllBytes($Config, $original)
        Start-Sleep -Seconds 3
    }
    finally {
        if (Test-Path -LiteralPath $Config -PathType Leaf) {
            try { $restored = (Get-FileHash -LiteralPath $Config -Algorithm SHA256).Hash.ToLowerInvariant() } catch {}
        }
        $equal = $restored -eq $backupHash -and $restored -eq $expected
        [ordered]@{ backup_sha256 = $backupHash; config_sha256 = $restored; equal = [int]$equal; expected_hash_matches = [int]($restored -eq $expected) } |
            ConvertTo-Json -Compress | Write-Output
        [ordered]@{ removed = [int](-not $equal); backup_sha256 = $backupHash; config_sha256 = $restored; equal = [int]$equal } |
            ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8
    }
    if (-not $equal) { throw 'RESTORE VERIFICATION FAILED.' }
}
