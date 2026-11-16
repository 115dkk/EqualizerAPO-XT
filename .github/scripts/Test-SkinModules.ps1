param([string] $RepositoryRoot = (Join-Path $PSScriptRoot '..\..'))

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'SkinModuleVerdict.psm1') -Force

$repositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$verdict = Get-SkinModuleVerdict (Get-SkinModuleMeasurements $repositoryRoot)
foreach ($failure in $verdict.Reasons) { Write-Host "::error::$failure" }
if (-not $verdict.Passed) {
    Write-Host "Skin module checks failed: $($verdict.Reasons.Count)"
    exit 1
}

Write-Host "Skin module checks passed for $($verdict.SkinIds.Count) skins: $($verdict.SkinIds -join ', ')"
