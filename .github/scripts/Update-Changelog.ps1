# Move the accumulated "## Unreleased" entries into a dated version section.
#
# The CI version-bump job runs this right after Bump-Version.ps1 raises
# version.h, so a release always ships with its user-facing entries already
# filed under the version being cut. When version.h does not bump
# (docs/ci/chore/refactor-only pushes) this script is not run, so the entries
# stay under Unreleased and are folded into whichever version bumps next -
# exactly the manual convention this replaces (see CLAUDE.md).
#
# The transform is deliberately narrow: it lifts the body between the
# "## Unreleased" heading and the next "## " heading, inserts a
# "## v<Version> - <Date>" heading (em dash, matching the existing sections)
# directly under an emptied Unreleased, and leaves the body verbatim. An empty
# Unreleased is a no-op (a version with no user-facing entries gets no section
# rather than an empty one). A missing Unreleased heading is a hard error: the
# changelog is malformed and a silent pass would lose the release's entries.
#
# File bytes are preserved apart from the inserted heading: the original
# newline style (CRLF on a Windows checkout) and UTF-8-without-BOM encoding are
# detected and kept, so the diff is only the moved heading, never a whole-file
# reflow.

param(
  [Parameter(Mandatory = $true)][string]$Version,
  [Parameter(Mandatory = $true)][string]$Date,
  [string[]]$Paths = @("CHANGELOG.md", "CHANGELOG.ko.md")
)

$ErrorActionPreference = "Stop"

# U+2014 built from its code point so the script source's own encoding can
# never corrupt the separator that has to match the existing headings.
$emDash = [char]0x2014

function Update-ChangelogFile {
  param([string]$Path, [string]$Version, [string]$Date)

  if (-not (Test-Path -LiteralPath $Path)) {
    Write-Host "Changelog $Path not found; skipping."
    return
  }

  $bytes = [System.IO.File]::ReadAllBytes($Path)
  $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
  $text = [System.Text.Encoding]::UTF8.GetString($bytes)
  if ($hasBom) {
    $text = $text.TrimStart([char]0xFEFF)
  }
  $newline = if ($text -match "`r`n") { "`r`n" } else { "`n" }

  # Split into lines without their terminators so the body can be relocated
  # without disturbing anything else.
  $lines = [System.Collections.Generic.List[string]]($text -split "`r`n|`n")

  $unreleasedIndex = -1
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^##\s+Unreleased\s*$') { $unreleasedIndex = $i; break }
  }
  if ($unreleasedIndex -lt 0) {
    throw "No '## Unreleased' section found in $Path"
  }

  $nextSectionIndex = $lines.Count
  for ($i = $unreleasedIndex + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^##\s') { $nextSectionIndex = $i; break }
  }

  # Body = everything strictly between the Unreleased heading and the next
  # section heading, then trimmed of the blank lines that pad the section.
  $body = [System.Collections.Generic.List[string]]::new()
  for ($i = $unreleasedIndex + 1; $i -lt $nextSectionIndex; $i++) {
    $body.Add($lines[$i])
  }
  while ($body.Count -gt 0 -and [string]::IsNullOrWhiteSpace($body[0])) {
    $body.RemoveAt(0)
  }
  while ($body.Count -gt 0 -and [string]::IsNullOrWhiteSpace($body[$body.Count - 1])) {
    $body.RemoveAt($body.Count - 1)
  }

  if ($body.Count -eq 0) {
    Write-Host "Unreleased section in $Path is empty; nothing to move."
    return
  }

  $result = [System.Collections.Generic.List[string]]::new()
  # Everything through the "## Unreleased" heading.
  for ($i = 0; $i -le $unreleasedIndex; $i++) {
    $result.Add($lines[$i])
  }
  $result.Add("")
  $result.Add("## v$Version $emDash $Date")
  $result.Add("")
  foreach ($line in $body) { $result.Add($line) }
  $result.Add("")
  # The next section heading onward, verbatim (includes the file's final
  # newline as a trailing empty element when the file ended with one).
  for ($i = $nextSectionIndex; $i -lt $lines.Count; $i++) {
    $result.Add($lines[$i])
  }

  $out = ($result -join $newline)
  $encoding = New-Object System.Text.UTF8Encoding($hasBom)
  [System.IO.File]::WriteAllText($Path, $out, $encoding)
  Write-Host "Filed $($body.Count) Unreleased line(s) under v$Version in $Path"
}

foreach ($path in $Paths) {
  Update-ChangelogFile -Path $path -Version $Version -Date $Date
}
