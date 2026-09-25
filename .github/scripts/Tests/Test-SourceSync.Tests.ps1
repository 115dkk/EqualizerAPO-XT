# Pester 5 tests for .github/scripts/Test-SourceSync.ps1
#
# The lint guards the one list this repository keeps by hand in two places: the
# engine sources Common.vcxproj compiles, and the ../ SOURCES entries Editor.pro
# compiles itself because the Editor deliberately does not link Common.lib
# (audit #146 TD013). The cases below lock the contract CI depends on - a
# matching pair passes, a dropped entry fails, separator and case differences are
# absorbed, Editor-only ../ sources are not an error, and the known-omission list
# has to stay true on both sides.
#
# Every case builds a temporary fixture tree instead of reading the real project
# files, so the tests keep their meaning when someone legitimately adds an engine
# source. The script runs out of process through `pwsh -Command ". '<script>'"`,
# the same shape GitHub's `shell: pwsh` uses, so the exit-code assertions
# describe what the workflow step actually gets when the lint throws.

BeforeAll {
    $script:ScriptPath = (Resolve-Path (Join-Path $PSScriptRoot '..' 'Test-SourceSync.ps1')).Path
    $script:PwshExe = (Get-Process -Id $PID).Path
    $script:TempRoots = [System.Collections.Generic.List[string]]::new()

    # Mirrors $knownEditorOmissions in the script under test. Fixtures must carry
    # these on the Common side and only there, otherwise the lint reports the
    # list itself as stale - which is the behaviour two of the cases below check.
    $script:KnownOmissions = @(
        'stdafx.cpp'
    )

    function New-FixtureRepo {
        param(
            [string[]]$CommonSources,
            [string[]]$EditorSources,
            [switch]$SkipKnownOmissions
        )
        $root = Join-Path ([System.IO.Path]::GetTempPath()) ("sourcesync-" + [guid]::NewGuid().ToString('N'))
        $script:TempRoots.Add($root)
        New-Item -ItemType Directory -Path (Join-Path $root 'Editor') -Force | Out-Null

        $allCommon = @($CommonSources)
        if (-not $SkipKnownOmissions) { $allCommon += $script:KnownOmissions }

        # Backslash separators and an ItemDefinitionGroup <ClCompile> without an
        # Include, exactly like the real Common.vcxproj.
        $items = ($allCommon | ForEach-Object { '    <ClCompile Include="' + ($_ -replace '/', '\') + '" />' }) -join "`r`n"
        $vcxproj = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemDefinitionGroup>
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
$items
  </ItemGroup>
</Project>
"@
        # UTF-8 with BOM, like the real file.
        [System.IO.File]::WriteAllText((Join-Path $root 'Common.vcxproj'), $vcxproj, (New-Object System.Text.UTF8Encoding($true)))
        # Every project's entries must exist on disk (audit #348 TD-74), so the
        # fixture carries the sources Common.vcxproj lists.
        foreach ($source in $allCommon) {
            $sourcePath = Join-Path $root $source
            New-Item -ItemType Directory -Path (Split-Path -Parent $sourcePath) -Force | Out-Null
            [System.IO.File]::WriteAllText($sourcePath, '')
        }

        # A qmake SOURCES block: tab-indented continuation lines, Editor-local
        # entries mixed in, and a final line without the trailing backslash.
        $lines = @('SOURCES += main.cpp\')
        $lines += ($EditorSources | ForEach-Object { "`t$_ \" })
        $lines += "`tMainWindow.cpp"
        $lines += ''
        $lines += '# ../filters/CommentedOut.cpp'
        [System.IO.File]::WriteAllLines((Join-Path $root 'Editor' 'Editor.pro'), $lines)

        return $root
    }

    function Write-Project {
        param([string]$Path, [string[]]$Includes)
        New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
        $items = ($Includes | ForEach-Object { '    <ClCompile Include="' + $_ + '" />' }) -join "`r`n"
        $xml = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
$items
  </ItemGroup>
</Project>
"@
        [System.IO.File]::WriteAllText($Path, $xml, (New-Object System.Text.UTF8Encoding($true)))
    }

    function Invoke-SourceSync {
        param([string]$RepoRoot)
        $output = & $script:PwshExe -NoProfile -NonInteractive -Command ". '$script:ScriptPath' -RepoRoot '$RepoRoot'" 2>&1
        return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output -join "`n") }
    }
}

AfterAll {
    foreach ($root in $script:TempRoots) {
        if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
    }
}

Describe "Test-SourceSync.ps1" {
    It "passes when Editor.pro carries every engine source Common.vcxproj compiles" {
        $root = New-FixtureRepo `
            -CommonSources @('FilterEngine.cpp', 'filters/BiQuadFilter.cpp', 'services/logging/Logging.cpp') `
            -EditorSources @('../FilterEngine.cpp', '../filters/BiQuadFilter.cpp', '../services/logging/Logging.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Be 0
        $result.Output | Should -Match "all 3 shared engine sources"
    }

    It "fails and names the file when Editor.pro is missing one engine source" {
        $root = New-FixtureRepo `
            -CommonSources @('FilterEngine.cpp', 'filters/BiQuadFilter.cpp', 'services/logging/Logging.cpp') `
            -EditorSources @('../FilterEngine.cpp', '../services/logging/Logging.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        # A non-zero exit code is the whole point in CI: the workflow step runs
        # this the same way and must not swallow the throw.
        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match ([regex]::Escape("::error file=Editor/Editor.pro::"))
        $result.Output | Should -Match ([regex]::Escape("filters/BiQuadFilter.cpp"))
    }

    It "does not fail on Editor-only sources that Common.vcxproj never compiles" {
        $root = New-FixtureRepo `
            -CommonSources @('FilterEngine.cpp') `
            -EditorSources @(
                '../FilterEngine.cpp'
                '../services/windows/WindowsService.cpp'
                '../services/install/ApoRegistration.cpp'
                '../services/audio/AudioFormatProbe.cpp'
                '../services/update/UpdateSession.cpp'
                '../services/update/VelopackBootstrap.cpp'
            )
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Be 0
    }

    It "absorbs the separator and case differences between the two file formats" {
        $root = New-FixtureRepo `
            -CommonSources @('engine/FilterEngine.Process.cpp', 'filters/loudnessCorrection/VolumeController.cpp') `
            -EditorSources @('../engine/filterengine.process.cpp', '../filters/loudnesscorrection/VolumeController.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Be 0
    }

    It "fails when a known omission has become an Editor.pro source" {
        $root = New-FixtureRepo `
            -CommonSources @('FilterEngine.cpp') `
            -EditorSources @('../FilterEngine.cpp', '../stdafx.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "known omission but Editor.pro now compiles it"
    }

    It "fails when a known omission is no longer compiled by Common.vcxproj" {
        $root = New-FixtureRepo -SkipKnownOmissions `
            -CommonSources @('FilterEngine.cpp') `
            -EditorSources @('../FilterEngine.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "no longer compiles it"
    }

    It "reports a test project that lists a source which is not on disk" {
        $root = New-FixtureRepo `
            -CommonSources @('FilterEngine.cpp') `
            -EditorSources @('../FilterEngine.cpp')
        # A test project alongside the fixture, listing one file that exists and
        # one that does not - the shape a rename leaves behind.
        $projectDir = Join-Path $root 'Tests' 'EditorLogicTests'
        New-Item -ItemType Directory -Path $projectDir -Force | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $projectDir 'Present.cpp'), '')
        $testProject = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <ClCompile Include="Present.cpp" />
    <ClCompile Include="..\..\Editor\Renamed.cpp" />
  </ItemGroup>
</Project>
"@
        [System.IO.File]::WriteAllText((Join-Path $projectDir 'EditorLogicTests.vcxproj'), $testProject, (New-Object System.Text.UTF8Encoding($true)))

        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "which is not on disk"
        $result.Output | Should -Match ([regex]::Escape("Renamed.cpp"))
    }

    It "passes when every source a test project lists exists" {
        $root = New-FixtureRepo `
            -CommonSources @('FilterEngine.cpp') `
            -EditorSources @('../FilterEngine.cpp')
        $projectDir = Join-Path $root 'Tests' 'HybridConvTests'
        New-Item -ItemType Directory -Path $projectDir -Force | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $projectDir 'Suite.cpp'), '')
        $testProject = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <ClCompile Include="Suite.cpp" />
    <ClCompile Include="..\..\FilterEngine.cpp" />
  </ItemGroup>
</Project>
"@
        [System.IO.File]::WriteAllText((Join-Path $projectDir 'HybridConvTests.vcxproj'), $testProject, (New-Object System.Text.UTF8Encoding($true)))
        [System.IO.File]::WriteAllText((Join-Path $root 'FilterEngine.cpp'), '')

        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Be 0
        $result.Output | Should -Match "listed sources and headers all exist"
    }

    It "checks every project in the tree, not only the test suites" {
        $root = New-FixtureRepo -CommonSources @('FilterEngine.cpp') -EditorSources @('../FilterEngine.cpp')
        Write-Project -Path (Join-Path $root 'Benchmark' 'Benchmark.vcxproj') -Includes @('Benchmark.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match ([regex]::Escape("Benchmark/Benchmark.vcxproj lists Benchmark.cpp, which is not on disk"))
    }

    It "reports an entry a project lists twice" {
        $root = New-FixtureRepo -CommonSources @('FilterEngine.cpp') -EditorSources @('../FilterEngine.cpp')
        $projectDir = Join-Path $root 'Tests' 'EditorLogicTests'
        Write-Project -Path (Join-Path $projectDir 'EditorLogicTests.vcxproj') -Includes @('Suite.cpp', '..\..\FilterEngine.cpp', '..\..\filterengine.cpp')
        [System.IO.File]::WriteAllText((Join-Path $projectDir 'Suite.cpp'), '')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "twice"
    }

    It "reports a .filters entry its project does not list" {
        $root = New-FixtureRepo -CommonSources @('FilterEngine.cpp') -EditorSources @('../FilterEngine.cpp')
        Write-Project -Path (Join-Path $root 'Common.vcxproj.filters') -Includes @('FilterEngine.cpp', 'Removed.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match ([regex]::Escape("Common.vcxproj.filters groups Removed.cpp"))
    }

    It "reports a .filters file whose project is gone" {
        $root = New-FixtureRepo -CommonSources @('FilterEngine.cpp') -EditorSources @('../FilterEngine.cpp')
        Write-Project -Path (Join-Path $root 'DeviceSelector' 'DeviceSelector.vcxproj.filters') -Includes @('main.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "which is not in the repository"
    }

    It "passes when a .filters file groups a subset of its project" {
        $root = New-FixtureRepo -CommonSources @('FilterEngine.cpp', 'Other.cpp') -EditorSources @('../FilterEngine.cpp', '../Other.cpp')
        Write-Project -Path (Join-Path $root 'Common.vcxproj.filters') -Includes @('FilterEngine.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Be 0
        $result.Output | Should -Match "list only their projects' entries"
    }

    It "requires every EAPO_ variable the code reads to be in the environment variable list" {
        $root = New-FixtureRepo -CommonSources @('FilterEngine.cpp') -EditorSources @('../FilterEngine.cpp')
        New-Item -ItemType Directory -Path (Join-Path $root 'docs') -Force | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $root 'docs' 'EnvironmentVariables.md'), "- ``EAPO_KNOWN`` - listed.")
        [System.IO.File]::WriteAllText((Join-Path $root 'FilterEngine.cpp'),
            'auto a = qEnvironmentVariableIsSet("EAPO_KNOWN"); auto b = GetEnvironmentVariableW(L"EAPO_HIDDEN", v, 8);')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "reads EAPO_HIDDEN"
        $result.Output | Should -Not -Match "reads EAPO_KNOWN"

        [System.IO.File]::WriteAllText((Join-Path $root 'docs' 'EnvironmentVariables.md'), "- ``EAPO_KNOWN``, ``EAPO_HIDDEN``")
        $result = Invoke-SourceSync -RepoRoot $root
        $result.ExitCode | Should -Be 0
        $result.Output | Should -Match "All 2 EAPO_\* environment variables"
    }

    It "fails on a second licence header or a second SPDX licence in one source" {
        $root = New-FixtureRepo -CommonSources @('FilterEngine.cpp') -EditorSources @('../FilterEngine.cpp')
        $header = "/*`n`tThis file is part of EqualizerAPO-XT, a system-wide equalizer.`n`tSPDX-License-Identifier: GPL-2.0-or-later`n*/`n"
        [System.IO.File]::WriteAllText((Join-Path $root 'FilterEngine.cpp'), $header + "/*`n`tThis file is part of EqualizerAPO-XT, a system-wide equalizer.`n*/`n")
        [System.IO.File]::WriteAllText((Join-Path $root 'Vendor.h'), $header + "/* SPDX-License-Identifier: BSD-2-Clause */`n")
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match ([regex]::Escape("::error file=FilterEngine.cpp::names the project in 2 licence headers"))
        $result.Output | Should -Match ([regex]::Escape("::error file=Vendor.h::declares 2 SPDX licences"))

        [System.IO.File]::WriteAllText((Join-Path $root 'FilterEngine.cpp'), $header)
        [System.IO.File]::WriteAllText((Join-Path $root 'Vendor.h'), "/* SPDX-License-Identifier: BSD-2-Clause */`n")
        $result = Invoke-SourceSync -RepoRoot $root
        $result.ExitCode | Should -Be 0
        $result.Output | Should -Match "at most one licence header and one SPDX licence"
    }

    It "refuses to pass when it could not read any ClCompile entry" {
        $root = New-FixtureRepo -SkipKnownOmissions -CommonSources @() -EditorSources @('../FilterEngine.cpp')
        $result = Invoke-SourceSync -RepoRoot $root

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "checked nothing"
    }
}
