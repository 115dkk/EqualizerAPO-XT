<#
.SYNOPSIS
    Compares two skin-gallery folders by the SHA-256 hash of every PNG.

.DESCRIPTION
    The skin gallery (Editor.exe --skin-gallery <dir>, or
    .github/scripts/Invoke-EditorOffscreenTest.ps1 -Gate skin-gallery) is the
    proof that a refactor did not move a pixel: render before, render after,
    compare. This script lists the added, removed and changed PNGs and exits 1
    when any of them is not excused.

    A scene the gallery itself knows to vary between runs is listed in the
    gallery's nondeterministic.txt, one "<file name>: <reason>" per line. The
    union of both folders' lists excuses a changed scene; it never excuses an
    added or removed one, because a missing shot is not noise.

    Also usable for DeviceSelector's --skin-shots folders, which have no
    nondeterministic.txt (every shot is expected to match).

.EXAMPLE
    pwsh -File tools/Compare-SkinGallery.ps1 -Baseline skin-gallery-baseline -Candidate skin-gallery
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Baseline,
    [Parameter(Mandatory)] [string] $Candidate
)

$ErrorActionPreference = "Stop"

function Get-PngHashes([string] $folder) {
    if (-not (Test-Path -LiteralPath $folder -PathType Container)) {
        throw "Gallery folder not found: $folder"
    }
    $table = @{}
    foreach ($file in Get-ChildItem -LiteralPath $folder -Filter *.png -File) {
        $table[$file.Name] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    return $table
}

function Get-Nondeterministic([string] $folder) {
    $names = @{}
    $list = Join-Path $folder "nondeterministic.txt"
    if (Test-Path -LiteralPath $list) {
        foreach ($line in Get-Content -LiteralPath $list) {
            $trimmed = $line.Trim()
            if ($trimmed -eq "" -or $trimmed.StartsWith("#")) { continue }
            $name = ($trimmed -split ":", 2)[0].Trim()
            $names[$name] = $true
        }
    }
    return $names
}

$before = Get-PngHashes $Baseline
$after = Get-PngHashes $Candidate
$excused = Get-Nondeterministic $Baseline
foreach ($name in (Get-Nondeterministic $Candidate).Keys) { $excused[$name] = $true }

$added = @($after.Keys | Where-Object { -not $before.ContainsKey($_) } | Sort-Object)
$removed = @($before.Keys | Where-Object { -not $after.ContainsKey($_) } | Sort-Object)
$common = @($before.Keys | Where-Object { $after.ContainsKey($_) } | Sort-Object)
$changed = @($common | Where-Object { $before[$_] -ne $after[$_] })
$changedExcused = @($changed | Where-Object { $excused.ContainsKey($_) })
$changedFailing = @($changed | Where-Object { -not $excused.ContainsKey($_) })
$identical = $common.Count - $changed.Count

Write-Host "Baseline:  $Baseline ($($before.Count) PNGs)"
Write-Host "Candidate: $Candidate ($($after.Count) PNGs)"
Write-Host "Identical: $identical"
Write-Host "Changed (listed as nondeterministic): $($changedExcused.Count)"
Write-Host "Changed (not listed): $($changedFailing.Count)"
Write-Host "Added: $($added.Count)"
Write-Host "Removed: $($removed.Count)"
foreach ($name in $changedExcused) { Write-Host "  nondeterministic: $name" }
foreach ($name in $changedFailing) { Write-Host "  CHANGED: $name" }
foreach ($name in $added) { Write-Host "  ADDED: $name" }
foreach ($name in $removed) { Write-Host "  REMOVED: $name" }

if ($changedFailing.Count -gt 0 -or $added.Count -gt 0 -or $removed.Count -gt 0) {
    Write-Host "Gallery comparison FAILED"
    exit 1
}
Write-Host "Gallery comparison passed"
exit 0
