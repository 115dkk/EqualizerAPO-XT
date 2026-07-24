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
#>
param(
  [string]$RepoRoot = (Join-Path $PSScriptRoot ".." "..")
)

$ErrorActionPreference = "Stop"

# Sources Common.vcxproj compiles that Editor.pro is expected NOT to list. Each
# entry carries its reason; an exception without one is how such a list rots.
#
# The two MultiConvolution entries need the longer story. They have never been in
# Editor.pro (added by #130, still absent through #139, #162 and #187) and their
# absence cannot break the Editor link: the filter is constructed only by its
# factory, and the factory is reached only through the REGISTER_FILTER_FACTORY
# static in its own translation unit, so the pair is an island in the link graph -
# precisely the case the .pro's "a missing engine file fails the link loudly"
# guarantee does not cover. The absence is observable at runtime though (the
# Editor's FilterFactoryRegistry has no "MultiConvolution" keyword, so the
# analysis FilterEngine and FilterCardModel::isKnownConfigCommand do not
# recognise the line). No maintainer decision recording that as intended was
# found, so they are listed here to describe the tree as it is, not to bless it.
$knownEditorOmissions = [ordered]@{
  "filters/MultiConvolutionFilter.cpp"        = "reachable only through its factory's self-registration; never carried by Editor.pro"
  "filters/MultiConvolutionFilterFactory.cpp" = "self-registering leaf translation unit; never carried by Editor.pro"
  "stdafx.cpp"                                = "MSBuild's precompiled-header creator (/Yc stdafx.h); qmake builds its own PCH unit from Editor/stable.h"
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
