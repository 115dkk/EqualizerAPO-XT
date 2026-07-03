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
  param([string]$Name)

  $line = Select-String -Path version.h -Pattern "^\s*#define\s+$Name\s+(\d+)" | Select-Object -First 1
  if (-not $line) {
    throw "Could not find version part: $Name"
  }
  return $line.Matches[0].Groups[1].Value
}
