# Pester 5 tests for Get-PreviousInstallerAsset.ps1 and Test-VariantSync.ps1
# (audit #422 TD-43: both decided release outcomes with no test).
#
# Get-PreviousInstallerAsset.ps1 talks to gh and git; the cases replace both
# with functions in the calling scope, which PowerShell resolves before the
# executables, so each case states exactly what the release and the history
# look like. Test-VariantSync.ps1 runs out of process against a fixture tree,
# like the Test-SourceSync cases, so the exit code is what the workflow sees.

Describe "Get-PreviousInstallerAsset.ps1" {
    BeforeAll {
        $script:ScriptPath = (Resolve-Path (Join-Path $PSScriptRoot ".." "Get-PreviousInstallerAsset.ps1")).Path
        $script:ReleasesJson = '[{"tagName":"v2.0.0","isDraft":false,"isPrerelease":false},{"tagName":"v1.9.0","isDraft":false,"isPrerelease":false}]'

        function Invoke-Decision([string] $outputPath) {
            & $script:ScriptPath -Repository "owner/repo" -Tag "v2.0.0" -ReleaseSha "abc123" `
                -AssetName "EqualizerAPO-XT-Setup.exe" -OutputPath $outputPath
        }
    }

    BeforeEach {
        $script:Output = Join-Path ([System.IO.Path]::GetTempPath()) ("previnst-" + [guid]::NewGuid().ToString("N") + ".exe")
        $global:PrevInstFake = @{ Releases = $script:ReleasesJson; Assets = '{"assets":[{"name":"EqualizerAPO-XT-Setup.exe"}]}'; DiffExit = 0; Calls = [System.Collections.Generic.List[string]]::new() }
        function global:gh {
            $global:PrevInstFake.Calls.Add("gh $($args -join ' ')")
            $global:LASTEXITCODE = 0
            switch ($args[1]) {
                "list" { return $global:PrevInstFake.Releases }
                "view" { return $global:PrevInstFake.Assets }
                "download" { Set-Content -LiteralPath $args[($args.IndexOf("--output") + 1)] -Value "exe"; return }
            }
        }
        function global:git {
            $global:PrevInstFake.Calls.Add("git $($args -join ' ')")
            $global:LASTEXITCODE = if ($args[0] -eq "diff") { $global:PrevInstFake.DiffExit } else { 0 }
        }
    }

    AfterEach {
        Remove-Item function:global:gh, function:global:git -ErrorAction SilentlyContinue
        if (Test-Path $script:Output) { Remove-Item $script:Output -Force }
    }

    It "reuses the previous installer when none of its inputs changed" {
        $decision = Invoke-Decision $script:Output
        $decision.Reuse | Should -BeTrue
        $decision.PreviousTag | Should -Be "v1.9.0"
        $script:Output | Should -Exist
        ($global:PrevInstFake.Calls | Where-Object { $_ -like "git diff*" }) | Should -Match "v1.9.0 abc123 .*Installer release platform/windows"
    }

    It "builds fresh when an input changed" {
        $global:PrevInstFake.DiffExit = 1
        $decision = Invoke-Decision $script:Output
        $decision.Reuse | Should -BeFalse
        $decision.Reason | Should -Be "installer inputs changed since v1.9.0"
        $script:Output | Should -Not -Exist
    }

    It "builds fresh when the previous release has no installer asset" {
        $global:PrevInstFake.Assets = '{"assets":[{"name":"Other.zip"}]}'
        $decision = Invoke-Decision $script:Output
        $decision.Reuse | Should -BeFalse
        $decision.Reason | Should -Be "previous release has no EqualizerAPO-XT-Setup.exe"
    }

    It "builds fresh when git diff fails for another reason" {
        $global:PrevInstFake.DiffExit = 128
        $decision = Invoke-Decision $script:Output
        $decision.Reuse | Should -BeFalse
        $decision.Reason | Should -Be "git diff against v1.9.0 failed"
    }
}

Describe "Test-VariantSync.ps1" {
    BeforeAll {
        $script:LintPath = (Resolve-Path (Join-Path $PSScriptRoot ".." "Test-VariantSync.ps1")).Path
        $script:ManifestPath = (Resolve-Path (Join-Path $PSScriptRoot ".." ".." "simd-variants.psd1")).Path
        $script:PwshExe = (Get-Process -Id $PID).Path
        $script:Channels = @((Import-PowerShellDataFile -Path $script:ManifestPath).Variants | ForEach-Object Channel | Sort-Object -Unique)

        function New-VariantFixture([string[]] $channels) {
            $root = Join-Path ([System.IO.Path]::GetTempPath()) ("variantsync-" + [guid]::NewGuid().ToString("N"))
            New-Item -ItemType Directory -Force -Path (Join-Path $root ".github"), (Join-Path $root "Installer") | Out-Null
            Copy-Item $script:ManifestPath (Join-Path $root ".github" "simd-variants.psd1")
            $literals = ($channels | ForEach-Object { "`tL`"$_`"," }) -join "`n"
            Set-Content -LiteralPath (Join-Path $root "Installer" "AutoInstallerLogic.cpp") -Value "static const wchar_t* channels[] = {`n$literals`n};"
            Set-Content -LiteralPath (Join-Path $root "Common.vcxproj") -Value "<Project><ItemGroup><Lib>Common.lib</Lib></ItemGroup></Project>"
            $root
        }

        function Invoke-VariantSync([string] $root) {
            $output = & $script:PwshExe -NoProfile -NonInteractive -Command ". '$script:LintPath' -RepoRoot '$root'" 2>&1
            [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output -join "`n") }
        }
    }

    It "passes when the installer names exactly the manifest's channels" {
        $result = Invoke-VariantSync (New-VariantFixture $script:Channels)
        $result.ExitCode | Should -Be 0
        $result.Output | Should -Match "channels match simd-variants.psd1"
    }

    It "fails when the installer lacks a manifest channel" {
        $result = Invoke-VariantSync (New-VariantFixture ($script:Channels | Select-Object -Skip 1))
        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match ([regex]::Escape("Missing manifest channels: $($script:Channels[0])"))
    }

    It "fails when another installer source names a channel the manifest does not have" {
        $root = New-VariantFixture $script:Channels
        Set-Content -LiteralPath (Join-Path $root "Installer" "Preview.cpp") -Value 'const wchar_t* preview = L"x64-avx9000";'
        $result = Invoke-VariantSync $root
        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "Channel not in simd-variants.psd1: x64-avx9000"
    }

    It "fails on a per-project EnableEnhancedInstructionSet literal" {
        $root = New-VariantFixture $script:Channels
        New-Item -ItemType Directory -Force -Path (Join-Path $root "Tool") | Out-Null
        Set-Content -LiteralPath (Join-Path $root "Tool" "Tool.vcxproj") -Value "<Project><ClCompile><EnableEnhancedInstructionSet>AdvancedVectorExtensions2</EnableEnhancedInstructionSet></ClCompile></Project>"
        $result = Invoke-VariantSync $root
        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "Literal <EnableEnhancedInstructionSet> found"
    }

    It "fails when a project outside Common.vcxproj spells the engine link contract" {
        $root = New-VariantFixture $script:Channels
        New-Item -ItemType Directory -Force -Path (Join-Path $root "Tool") | Out-Null
        Set-Content -LiteralPath (Join-Path $root "Tool" "Tool.vcxproj") -Value "<Project><Link><AdditionalDependencies>Common.lib</AdditionalDependencies></Link></Project>"
        $result = Invoke-VariantSync $root
        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "Engine link contract spelled in a project file"
    }

    It "fails on a per-configuration-and-platform ItemDefinitionGroup" {
        $root = New-VariantFixture $script:Channels
        New-Item -ItemType Directory -Force -Path (Join-Path $root "Tool") | Out-Null
        Set-Content -LiteralPath (Join-Path $root "Tool" "Tool.vcxproj") -Value "<Project><ItemDefinitionGroup Condition=`"'`$(Configuration)|`$(Platform)'=='Release|x64'`"><ClCompile /></ItemDefinitionGroup></Project>"
        $result = Invoke-VariantSync $root
        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "a per-configuration-and-platform ItemDefinitionGroup"
    }

    It "fails on a project-wide ConformanceMode" {
        $root = New-VariantFixture $script:Channels
        New-Item -ItemType Directory -Force -Path (Join-Path $root "Tool") | Out-Null
        Set-Content -LiteralPath (Join-Path $root "Tool" "Tool.vcxproj") -Value "<Project><ItemDefinitionGroup Condition=`"'`$(Configuration)'=='Release'`"><ClCompile><ConformanceMode>true</ConformanceMode></ClCompile></ItemDefinitionGroup></Project>"
        $result = Invoke-VariantSync $root
        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "Project setting shape: Tool.Tool\.vcxproj \(<ConformanceMode>\)"
    }

    It "lints the projects of a checkout that itself lives under .claude" {
        $outer = New-VariantFixture $script:Channels
        $root = Join-Path $outer ".claude" "worktrees" "wt"
        New-Item -ItemType Directory -Force -Path $root | Out-Null
        Get-ChildItem -LiteralPath $outer -Force | Where-Object Name -ne ".claude" | Move-Item -Destination $root
        New-Item -ItemType Directory -Force -Path (Join-Path $root "Tool") | Out-Null
        Set-Content -LiteralPath (Join-Path $root "Tool" "Tool.vcxproj") -Value "<Project><ClCompile><PreprocessorDefinitions>NOMINMAX;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></Project>"
        $result = Invoke-VariantSync $root
        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "Project setting shape: Tool.Tool\.vcxproj \(NOMINMAX\)"
    }

    It "passes a LanguageStandard pinned on one ClCompile item and a platform delta group" {
        $root = New-VariantFixture $script:Channels
        New-Item -ItemType Directory -Force -Path (Join-Path $root "Tool") | Out-Null
        Set-Content -LiteralPath (Join-Path $root "Tool" "Tool.vcxproj") -Value "<Project><ItemDefinitionGroup Condition=`"'`$(Configuration)'=='Release' And '`$(Platform)'=='ARM64'`"><ClCompile /></ItemDefinitionGroup><ItemGroup><ClCompile Include=`"Slow.cpp`"><LanguageStandard>stdcpp17</LanguageStandard></ClCompile></ItemGroup></Project>"
        $result = Invoke-VariantSync $root
        $result.ExitCode | Should -Be 0
        $result.Output | Should -Match "Every project states its settings per configuration"
    }
}
