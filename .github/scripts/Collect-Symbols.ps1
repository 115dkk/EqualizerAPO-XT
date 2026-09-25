<#
.SYNOPSIS
    Collects the debug symbols of every program an artifact ships.

.DESCRIPTION
    Field crash minidumps can only be symbolized against the PDB of the exact
    shipped binary, so the symbols artifact keeps one for every program the
    artifact carries. The list is derived from Package-Artifacts.ps1's plan
    (its required files, the Win32 wrapper, the VST3 module and the Qt apps)
    rather than written here, so a program added to the artifact cannot ship
    without its symbols (audit #348 TD-75). The inline YAML step this
    replaced kept three of the nine; the ASIO wrapper and host, Benchmark,
    VoicemeeterClient and the VST3 module left theirs behind. A missing PDB
    fails the step.

    -PlanOnly returns the source and destination pairs, for Pester.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)] [ValidateSet("x64", "ARM64")] [string] $Platform,
    [Parameter(Mandatory)] [string] $SimdVariant,
    [string] $SymbolsDirectory = "",
    [switch] $PlanOnly
)

$package = & (Join-Path $PSScriptRoot "Package-Artifacts.ps1") `
    -WorkspaceRoot $WorkspaceRoot -Platform $Platform -SimdVariant $SimdVariant -PlanOnly

# MSBuild and qmake both name a module's PDB after the module.
function Get-SymbolFile([string] $binary) {
    return [System.IO.Path]::ChangeExtension($binary, ".pdb")
}

$symbols = @()
foreach ($binary in @($package.RequiredFiles) + @($package.Vst3PluginModule)) {
    $pdb = Get-SymbolFile $binary
    $symbols += [pscustomobject]@{ Source = $pdb; Destination = [System.IO.Path]::GetFileName($pdb) }
}
# The x86 wrapper has the same file name as the x64 one; its symbols go
# where the artifact puts it.
if ($package.Win32Wrapper) {
    $pdb = Get-SymbolFile $package.Win32Wrapper
    $symbols += [pscustomobject]@{ Source = $pdb; Destination = "x86\" + [System.IO.Path]::GetFileName($pdb) }
}
foreach ($app in $package.QtApps) {
    $symbols += [pscustomobject]@{ Source = "build-$app-$Platform\release\$app.pdb"; Destination = "$app.pdb" }
}

if ($PlanOnly) { return $symbols }

$ErrorActionPreference = "Stop"
if (-not $SymbolsDirectory) { $SymbolsDirectory = Join-Path $WorkspaceRoot "symbols" }
$missing = @()
foreach ($symbol in $symbols) {
    $source = Join-Path $WorkspaceRoot $symbol.Source
    if (-not (Test-Path -LiteralPath $source)) {
        $missing += $symbol.Source
        continue
    }
    $destination = Join-Path $SymbolsDirectory $symbol.Destination
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}
if ($missing.Count -gt 0) {
    foreach ($path in $missing) {
        Write-Host "::error::Debug symbols not found: $path. A shipped program without its PDB cannot be symbolized from a crash dump."
    }
    throw "$($missing.Count) shipped program(s) left no debug symbols."
}
Get-ChildItem -LiteralPath $SymbolsDirectory -Recurse -File | ForEach-Object { $_.FullName.Substring($SymbolsDirectory.Length + 1) }
