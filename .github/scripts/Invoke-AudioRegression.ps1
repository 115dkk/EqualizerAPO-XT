<#
.SYNOPSIS
    Runs AudioRegressionTests for one build leg: verifies against the
    committed references, or records them when the workflow was dispatched to.

.DESCRIPTION
    The mode used to be decided inline in build.yml (audit #348 TD-75); it is
    decided here so Pester pins the rules.

    - Regenerate requested: record. The primary variant re-records the golden
      set; any other variant records its own output only.
    - Otherwise the committed set (references\cases.json) must exist and the
      run verifies against it. A missing set fails the run instead of being
      seeded again (audit #275 TD-08).

    -PlanOnly returns the mode and the arguments without running anything.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)] [string] $Platform,
    [Parameter(Mandatory)] [string] $SimdVariant,
    [switch] $Primary,
    [switch] $Regenerate,
    [switch] $PlanOnly
)

$suiteDirectory = Join-Path $WorkspaceRoot "Tests\AudioRegressionTests"
$executable = Join-Path $suiteDirectory "$Platform\Release\AudioRegressionTests.exe"
$hasReferences = Test-Path -LiteralPath (Join-Path $suiteDirectory "references\cases.json")
$arguments = @("--variant", $SimdVariant, "--config-dir", "configs", "--ref-dir", "references", "--out-dir", "output")
$mode = if ($Regenerate) {
    if ($Primary) { "RecordReferences" } else { "RecordVariant" }
} elseif ($hasReferences) {
    "Verify"
} else {
    "MissingReferences"
}
if ($mode -like "Record*") { $arguments += "--generate-references" }

$plan = [pscustomobject]@{
    Mode = $mode
    Executable = $executable
    Arguments = $arguments
    WorkingDirectory = $suiteDirectory
}
if ($PlanOnly) { return $plan }

$ErrorActionPreference = "Stop"
switch ($mode) {
    "MissingReferences" {
        Write-Error "references/cases.json is missing and regenerate_references was not requested. Restore the committed reference set, or dispatch the workflow with regenerate_references=true if re-recording is genuinely intended."
    }
    "RecordReferences" {
        Write-Host "::warning::regenerate_references was requested; primary variant '$SimdVariant' is RE-RECORDING the golden references instead of verifying against them. See Tests/AudioRegressionTests/references/README.md before committing the result."
    }
    "RecordVariant" {
        Write-Host "::warning::regenerate_references was requested; recording variant output only (non-primary variant '$SimdVariant')"
    }
    "Verify" {
        Write-Host "Running verify mode for variant '$SimdVariant'"
    }
}
if (-not (Test-Path -LiteralPath $executable)) {
    Write-Error "AudioRegressionTests executable not found at $executable"
}

$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$env:MUPARSERX_LIB;$env:PATH"
Push-Location $suiteDirectory
try {
    & $executable @arguments
    if ($LASTEXITCODE -ne 0) {
        $what = if ($mode -eq "Verify") { "verify" } else { "reference generation" }
        Write-Host "::error::AudioRegressionTests $what failed for variant '$SimdVariant'"
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}
