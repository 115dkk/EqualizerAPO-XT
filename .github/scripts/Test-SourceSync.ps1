<#
.SYNOPSIS
    Fails when an engine source Common.vcxproj compiles is missing from
    Editor/Editor.pro.

.DESCRIPTION
    The Editor deliberately does not link Common.lib: it compiles the engine
    sources itself so the analysis panel's FilterEngine runs under the Editor's
    own SIMD flags (audit #146 TD013, maintainer decision 2026-07-04, recorded in
    Editor.pro). The price of that decision is one engine source list maintained
    by hand in two files, and the two have drifted before (d66a523, b7c04a4).

    The drift is asymmetric, which is what makes it expensive: MSBuild stays
    green, and only the qmake Editor build notices, twenty minutes into a matrix
    leg on the runner, at the link step. This lint runs before the matrix starts.

    Only the Common.vcxproj -> Editor.pro direction is an error. The reverse is
    normal: Editor.pro also reaches outside the Editor directory for helpers
    Common.vcxproj does not compile at all (WindowsServiceControl, ApoRegistration,
    AudioFormatProbe, services/update - shared with DeviceSelector), so a ../
    entry with no ClCompile behind it is not reported.

    It then checks that every source and header the test projects list actually
    exists. Those lists are hand-written too and nothing checked them: a renamed
    or moved file leaves a stale entry that only surfaces as a compiler error
    partway through a matrix leg.
#>
param(
  [string]$RepoRoot = (Join-Path $PSScriptRoot ".." "..")
)

$ErrorActionPreference = "Stop"

# Sources Common.vcxproj compiles that Editor.pro is expected NOT to list. Each
# entry carries its reason; an exception without one is how such a list rots.
#
# Keep this list short. A self-registering translation unit that nothing names
# directly is an island in the link graph, so leaving it out of Editor.pro does
# not fail the link - it just silently removes the feature from the Editor. That
# is how the two MultiConvolution files went missing from #130 until this lint
# was written; they are now listed in Editor.pro rather than excused here.
$knownEditorOmissions = [ordered]@{
  "stdafx.cpp" = "MSBuild's precompiled-header creator (/Yc stdafx.h); qmake builds its own PCH unit from Editor/stable.h"
}

$projectPath = Join-Path $RepoRoot "Common.vcxproj"
$proPath = Join-Path $RepoRoot "Editor" "Editor.pro"

# XmlDocument.Load handles the file's BOM and encoding declaration itself, but it
# resolves a relative path against the process directory rather than the
# PowerShell one, so hand it a fully resolved path.
$project = New-Object System.Xml.XmlDocument
$project.Load((Resolve-Path -LiteralPath $projectPath).ProviderPath)
# local-name() spares us an XmlNamespaceManager for the single MSBuild namespace;
# requiring @Include skips the ItemDefinitionGroup <ClCompile> setting blocks.
$commonSources = @(
  $project.SelectNodes("//*[local-name()='ClCompile'][@Include]") |
    ForEach-Object { $_.Include -replace '\\', '/' }
)

if ($commonSources.Count -eq 0) {
  throw "No <ClCompile Include=...> entries found in $projectPath, so this lint checked nothing."
}

# qmake lists the engine sources as "../<path>.cpp" continuation lines. Anchoring
# both ends keeps a ../ inside a comment or a variable assignment out of the set.
$proText = Get-Content -LiteralPath $proPath -Raw
$editorSources = @(
  [regex]::Matches($proText, '(?m)^\s*\.\./(\S+\.cpp)(?:\s*\\)?\s*$') |
    ForEach-Object { $_.Groups[1].Value }
)

# MSVC and qmake both treat these paths case-insensitively, so a case-only
# difference is not a build failure and must not be reported as one.
$editorLookup = [System.Collections.Generic.HashSet[string]]::new(
  [string[]]$editorSources, [System.StringComparer]::OrdinalIgnoreCase)

$missingInEditor = @($commonSources | Where-Object {
  -not $editorLookup.Contains($_) -and -not $knownEditorOmissions.Contains($_)
})
$omissionsNowInEditor = @($knownEditorOmissions.Keys | Where-Object { $editorLookup.Contains($_) })
$omissionsGoneFromCommon = @($knownEditorOmissions.Keys | Where-Object { $commonSources -notcontains $_ })

foreach ($source in $missingInEditor) {
  Write-Host "::error file=Editor/Editor.pro::Common.vcxproj compiles $source but Editor.pro does not. Add '../$source' to SOURCES, or record it in `$knownEditorOmissions in .github/scripts/Test-SourceSync.ps1 with its reason."
}
foreach ($source in $omissionsNowInEditor) {
  Write-Host "::error file=.github/scripts/Test-SourceSync.ps1::$source is recorded as a known omission but Editor.pro now compiles it. Drop it from `$knownEditorOmissions so the lint guards it from now on."
}
foreach ($source in $omissionsGoneFromCommon) {
  Write-Host "::error file=.github/scripts/Test-SourceSync.ps1::$source is recorded as a known omission but Common.vcxproj no longer compiles it. Drop it from `$knownEditorOmissions."
}

if ($missingInEditor.Count -gt 0 -or $omissionsNowInEditor.Count -gt 0 -or $omissionsGoneFromCommon.Count -gt 0) {
  throw "Common.vcxproj and Editor/Editor.pro engine source lists are out of sync."
}

$sharedCount = $commonSources.Count - $knownEditorOmissions.Count
Write-Host "Editor.pro compiles all $sharedCount shared engine sources from Common.vcxproj; known omissions: $($knownEditorOmissions.Keys -join ', ')."

# Every tracked MSBuild project, imported source list and .filters file is
# checked (audit #348 TD-74). The hand-written list this replaced named nine
# projects, so new probes and the product projects outside it went unchecked,
# and nothing looked at .filters files or at an entry listed twice. The Pester
# fixtures are plain folders rather than repositories, so without git the
# tree is walked instead.
function Get-ProjectFileList {
  param([string]$Root)
  $listed = @()
  if (Get-Command git -ErrorAction SilentlyContinue) {
    $listed = @(& git -C $Root ls-files -- '*.vcxproj' '*.props' '*.vcxproj.filters' 2>$null)
    if ($LASTEXITCODE -ne 0) { $listed = @() }
  }
  if ($listed.Count -eq 0) {
    $listed = @(Get-ChildItem -LiteralPath $Root -Recurse -File |
      Where-Object { $_.Name -like '*.vcxproj' -or $_.Name -like '*.props' -or $_.Name -like '*.vcxproj.filters' } |
      ForEach-Object { [System.IO.Path]::GetRelativePath($Root, $_.FullName) })
  }
  return @($listed | ForEach-Object { $_ -replace '\\', '/' })
}

# The ClCompile and ClInclude entries of one project file, each with the full
# path MSBuild resolves it to. MSBuild resolves a relative Include against the
# project directory; an imported source list spells its own directory as a
# property instead.
function Get-ProjectEntries {
  param([string]$Root, [string]$RelativePath)
  $path = Join-Path $Root $RelativePath
  $document = New-Object System.Xml.XmlDocument
  $document.Load((Resolve-Path -LiteralPath $path).ProviderPath)
  $directory = Split-Path -Parent $path
  foreach ($node in $document.SelectNodes("//*[local-name()='ClCompile' or local-name()='ClInclude'][@Include]")) {
    $include = $node.Include.Replace('$(MSBuildThisFileDirectory)', '')
    [pscustomobject]@{
      Include = $node.Include
      FullPath = [System.IO.Path]::GetFullPath((Join-Path $directory $include))
    }
  }
}

$projectFiles = Get-ProjectFileList -Root $RepoRoot
$projectErrors = @()
$checkedEntries = 0
$entriesByProject = @{}
foreach ($relative in @($projectFiles | Where-Object { $_ -notlike '*.filters' })) {
  $entries = @(Get-ProjectEntries -Root $RepoRoot -RelativePath $relative)
  $entriesByProject[$relative.ToLowerInvariant()] = $entries
  $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
  foreach ($entry in $entries) {
    $checkedEntries++
    if (-not (Test-Path -LiteralPath $entry.FullPath)) {
      $projectErrors += "::error file=$relative::$relative lists $($entry.Include), which is not on disk. Update the project's source list or restore the file."
    }
    if (-not $seen.Add($entry.FullPath)) {
      $projectErrors += "::error file=$relative::$relative lists $($entry.Include) twice. MSBuild compiles it once but the duplicate hides which entry is meant; remove one."
    }
  }
}

# A .filters file only groups the entries of its project for the IDE. An entry
# the project does not list is a leftover of a rename or a removal.
$checkedFilters = 0
foreach ($relative in @($projectFiles | Where-Object { $_ -like '*.vcxproj.filters' })) {
  $checkedFilters++
  $projectRelative = $relative.Substring(0, $relative.Length - '.filters'.Length)
  $projectEntries = $entriesByProject[$projectRelative.ToLowerInvariant()]
  if ($null -eq $projectEntries) {
    $projectErrors += "::error file=$relative::$relative belongs to $projectRelative, which is not in the repository. Delete it."
    continue
  }
  $known = [System.Collections.Generic.HashSet[string]]::new(
    [string[]]@($projectEntries | ForEach-Object { $_.FullPath }), [System.StringComparer]::OrdinalIgnoreCase)
  foreach ($entry in @(Get-ProjectEntries -Root $RepoRoot -RelativePath $relative)) {
    if (-not $known.Contains($entry.FullPath)) {
      $projectErrors += "::error file=$relative::$relative groups $($entry.Include), which $projectRelative does not list. Remove the entry or add the file to the project."
    }
  }
}

foreach ($message in $projectErrors) {
  Write-Host $message
}
if ($projectErrors.Count -gt 0) {
  throw "A project file lists a missing or duplicate source, or a .filters file lists what its project does not."
}

Write-Host "The $($entriesByProject.Count) project files' $checkedEntries listed sources and headers all exist, none is listed twice, and the $checkedFilters .filters files list only their projects' entries."

# Audit #250 F071 compared the Qt apps' .pro and .vcxproj source lists here.
# Audit #348 TD-29 deleted the .vcxproj files (and UpdateChecker itself), so
# DeviceSelector builds from its .pro alone and there is no second list.

# Audit #348 TD-23: TestHarness's "zero checks" backstop fires per harness, so
# it only catches a forgotten call in HybridConvTests, where every sub-suite
# owns a harness. EditorLogicTests shares one harness across every test
# function and EngineOrchestrationTests hands one to all its runners: there a
# test function that is defined but never called leaves the suite green. Every
# test function must be called somewhere in its suite besides its definition.
$sharedHarnessSuites = @(
  @{ Name = "EditorLogicTests"; Pattern = '(?m)^void (test\w+)\(\)\s*$' },
  @{ Name = "EngineOrchestrationTests"; Pattern = '(?m)^void ((?:test|run)\w+)\(test::Harness&\s*\w*\)\s*$' }
)
$uncalled = @()
$checkedTestFunctions = 0
$checkedSuites = 0
foreach ($suite in $sharedHarnessSuites) {
  $suiteDir = Join-Path $RepoRoot "Tests" $suite.Name
  # The Pester cases run this script against a minimal fake tree.
  if (-not (Test-Path -LiteralPath $suiteDir)) { continue }
  $checkedSuites++
  $files = @(Get-ChildItem -LiteralPath $suiteDir -Filter "*.cpp" -File)
  $texts = @{}
  foreach ($file in $files) { $texts[$file.Name] = Get-Content -LiteralPath $file.FullName -Raw }
  foreach ($file in $files) {
    foreach ($match in [regex]::Matches($texts[$file.Name], $suite.Pattern)) {
      $name = $match.Groups[1].Value
      $checkedTestFunctions++
      $callPattern = '(?<![\w:])' + [regex]::Escape($name) + '\s*\((?!\s*\)\s*$)'
      $called = $false
      foreach ($other in $files) {
        $text = $texts[$other.Name]
        # The definition line itself matches the pattern too; count calls only.
        $calls = [regex]::Matches($text, '(?m)^(?!void ).*' + $callPattern)
        if ($calls.Count -gt 0) { $called = $true; break }
      }
      if (-not $called) { $uncalled += "$($suite.Name): $name is defined in $($file.Name) but never called" }
    }
  }
}
if ($checkedSuites -gt 0 -and $checkedTestFunctions -eq 0) {
  throw "No test functions found in the shared-harness suites, so this lint checked nothing."
}
foreach ($entry in $uncalled) {
  Write-Host "::error::$entry"
}
if ($uncalled.Count -gt 0) {
  throw "A test function in a shared-harness suite is never called."
}
Write-Host "All $checkedTestFunctions test functions of the shared-harness suites are called."

# Audit #348 TD-80: docs/EnvironmentVariables.md calls itself the one list of
# EAPO_* variables, and ten the code reads were missing from it. Every EAPO_*
# name a C++ source reads from the environment must be written there.
$environmentDoc = Join-Path $RepoRoot "docs" "EnvironmentVariables.md"
if (Test-Path -LiteralPath $environmentDoc) {
  $documented = Get-Content -LiteralPath $environmentDoc -Raw
  $readPattern = '(?:qEnvironmentVariable\w*|qgetenv|GetEnvironmentVariableW?|_wgetenv|getenv)\(\s*(?:QStringLiteral\()?L?"(EAPO_[A-Z0-9_]+)"'
  $sources = @()
  if (Get-Command git -ErrorAction SilentlyContinue) {
    $sources = @(& git -C $RepoRoot ls-files -- '*.cpp' '*.h' 2>$null)
    if ($LASTEXITCODE -ne 0) { $sources = @() }
  }
  if ($sources.Count -eq 0) {
    $sources = @(Get-ChildItem -LiteralPath $RepoRoot -Recurse -File -Include '*.cpp', '*.h' |
      ForEach-Object { [System.IO.Path]::GetRelativePath($RepoRoot, $_.FullName) })
  }
  $read = @{}
  foreach ($source in $sources) {
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot $source) -Raw
    if ($null -eq $text -or $text -notmatch 'EAPO_') { continue }
    foreach ($match in [regex]::Matches($text, $readPattern)) {
      $read[$match.Groups[1].Value] = $source -replace '\\', '/'
    }
  }
  $undocumented = @($read.Keys | Where-Object { $documented -notmatch ('\b' + [regex]::Escape($_) + '\b') } | Sort-Object)
  foreach ($name in $undocumented) {
    Write-Host "::error file=docs/EnvironmentVariables.md::$($read[$name]) reads $name from the environment, but docs/EnvironmentVariables.md does not list it."
  }
  if ($undocumented.Count -gt 0) {
    throw "An EAPO_* environment variable the code reads is missing from docs/EnvironmentVariables.md."
  }
  Write-Host "All $($read.Count) EAPO_* environment variables the code reads are listed in docs/EnvironmentVariables.md."
}
