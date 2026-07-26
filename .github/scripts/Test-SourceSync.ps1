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
    Common.vcxproj does not compile at all (ServiceHelper, ApoRegistration,
    AudioFormatProbe, VelopackBootstrap - shared with DeviceSelector), so a ../
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

# The test projects keep their own hand-written source lists too, and those had
# nothing checking them at all. A sync against Editor.pro would be wrong - they
# compile a deliberate subset, the part that stands up without the Qt widget stack
# - so what is checked is the property that actually breaks: an entry that no
# longer exists on disk. That happens when a source is renamed or moved, and the
# failure it produces today is a compiler error twenty minutes into a matrix leg.
$testProjects = @(
  (Join-Path $RepoRoot "Tests" "EditorLogicTests" "EditorLogicTests.vcxproj"),
  (Join-Path $RepoRoot "Tests" "EngineOrchestrationTests" "EngineOrchestrationTests.vcxproj"),
  (Join-Path $RepoRoot "Tests" "HybridConvTests" "HybridConvTests.vcxproj"),
  (Join-Path $RepoRoot "Tests" "AudioRegressionTests" "AudioRegressionTests.vcxproj")
)

$missingTestSources = @()
$checkedTestSources = 0
foreach ($projectFile in $testProjects) {
  if (-not (Test-Path -LiteralPath $projectFile)) { continue }

  $testProject = New-Object System.Xml.XmlDocument
  $testProject.Load((Resolve-Path -LiteralPath $projectFile).ProviderPath)
  $projectDirectory = Split-Path -Parent $projectFile

  foreach ($node in $testProject.SelectNodes("//*[local-name()='ClCompile' or local-name()='ClInclude'][@Include]")) {
    # MSBuild resolves a relative Include against the project directory, and the
    # test projects reach out of theirs with ..\..\ for the sources they share.
    $resolved = Join-Path $projectDirectory $node.Include
    $checkedTestSources++
    if (-not (Test-Path -LiteralPath $resolved)) {
      $missingTestSources += [pscustomobject]@{
        Project = (Split-Path -Leaf $projectFile)
        Include = $node.Include
      }
    }
  }
}

foreach ($entry in $missingTestSources) {
  Write-Host "::error file=Tests/$($entry.Project)::$($entry.Project) lists $($entry.Include), which is not on disk. Update the project's source list or restore the file."
}

if ($missingTestSources.Count -gt 0) {
  throw "A test project lists a source that does not exist."
}

Write-Host "The test projects' $checkedTestSources listed sources and headers all exist."
