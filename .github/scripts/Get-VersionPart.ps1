<#
.SYNOPSIS
    Defines Get-VersionPart: reads one numeric component (MAJOR / MINOR /
    REVISION) out of version.h.

.DESCRIPTION
    Dot-source this file, then call the function with the #define name:

      . .\.github\scripts\Get-VersionPart.ps1
      $major = Get-VersionPart "MAJOR"

    version.h is resolved relative to the current directory (the repository
    root in CI). Shared by the version-bump and create-release jobs in
    .github/workflows/build.yml, so the version.h parsing is written exactly
    once.
#>
function Get-VersionPart {
  # Audit #250 F069: this used to have a second, signature-diverging
  # implementation inside Bump-Version.ps1. One parser now serves both
  # callers: pass -Lines when the caller already holds the file content
  # (Bump-Version rewrites it), or nothing to read version.h directly.
  param(
    [string]$Name,
    [string[]]$Lines
  )

  if (-not $Lines) {
    $Lines = Get-Content -Path version.h
  }

  foreach ($line in $Lines) {
    if ($line -match "^\s*#define\s+$Name\s+(\d+)") {
      return [int]$Matches[1]
    }
  }

  throw "Could not find version part: $Name"
}
