param(
  [string]$VersionHeader = "version.h",
  [switch]$Check
)

$ErrorActionPreference = "Stop"

function Invoke-Git {
  $safeDirectory = (Get-Location).Path -replace "\\", "/"
  & git -c "safe.directory=$safeDirectory" @args
}

function Get-VersionPart {
  param([string]$Name, [string[]]$Lines)

  foreach ($line in $Lines) {
    if ($line -match "^\s*#define\s+$Name\s+(\d+)\s*$") {
      return [int]$Matches[1]
    }
  }

  throw "Could not find version part $Name in $VersionHeader"
}

function Get-BumpKind {
  $lastTag = ""
  try {
    $lastTag = (& Invoke-Git describe --tags --match "v[0-9]*" --abbrev=0 2>$null)
  } catch {
    $lastTag = ""
  }

  $range = if ([string]::IsNullOrWhiteSpace($lastTag)) { "HEAD" } else { "$lastTag..HEAD" }
  $messages = @(& Invoke-Git log --format=%B $range)
  $joined = ($messages -join "`n")

  if ($joined -match "BREAKING CHANGE" -or $joined -match "(^|\n)\w+(\([^)]+\))?!:") {
    return "major"
  }
  if ($joined -match "(^|\n)feat(\([^)]+\))?:") {
    return "minor"
  }
  return "patch"
}

if (-not (Test-Path $VersionHeader)) {
  throw "Version header not found: $VersionHeader"
}

$lines = Get-Content -Path $VersionHeader
$major = Get-VersionPart "MAJOR" $lines
$minor = Get-VersionPart "MINOR" $lines
$revision = Get-VersionPart "REVISION" $lines

$bumpKind = Get-BumpKind
switch ($bumpKind) {
  "major" {
    $major += 1
    $minor = 0
    $revision = 0
  }
  "minor" {
    $minor += 1
    $revision = 0
  }
  default {
    $revision += 1
  }
}

$nextLines = foreach ($line in $lines) {
  if ($line -match "^\s*#define\s+MAJOR\s+\d+\s*$") {
    "#define MAJOR $major"
  } elseif ($line -match "^\s*#define\s+MINOR\s+\d+\s*$") {
    "#define MINOR $minor"
  } elseif ($line -match "^\s*#define\s+REVISION\s+\d+\s*$") {
    "#define REVISION $revision"
  } else {
    $line
  }
}

$nextVersion = "$major.$minor.$revision"
if ($Check) {
  Write-Host "Next $bumpKind version would be $nextVersion"
  exit 0
}

Set-Content -Path $VersionHeader -Value $nextLines -Encoding ASCII
Write-Host "Bumped $VersionHeader to $nextVersion using $bumpKind rule"
