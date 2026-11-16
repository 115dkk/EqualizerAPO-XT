Describe "extracted build script decisions" {
    BeforeAll {
        $root = Join-Path $TestDrive "repo"
    }

    It "selects native ARM64 toolchain and suppresses unsupported runtime tests" {
        $plan = & (Join-Path $PSScriptRoot "..\Build-Solution.ps1") `
            -WorkspaceRoot $root -Platform ARM64 -SimdVariant neon -CanExecute:$false -PlanOnly
        # The ARM64 image ships ATL for ARM64 only with the v145 toolset.
        $plan.PlatformToolset | Should -Be "v145"
        $plan.ToolArchitecture | Should -Be "ARM64"
        # A runner that cannot execute the variant runs nothing: EditorLogicTests
        # links Common.lib whole-archive and so now carries the variant's /arch
        # into a static initializer.
        $plan.RuntimeTests | Should -BeNullOrEmpty
        # No x86 cross-build on the ARM64 leg; the avx2 leg owns the installer.
        $plan.InstallerProject | Should -BeNullOrEmpty
    }

    It "builds the auto-detect installer on the avx2 leg only" {
        $avx2 = & (Join-Path $PSScriptRoot "..\Build-Solution.ps1") `
            -WorkspaceRoot $root -Platform x64 -SimdVariant avx2 -ArchFlag AdvancedVectorExtensions2 -PlanOnly
        $avx2.InstallerProject | Should -Be "Installer\Installer.vcxproj"
        $sse2 = & (Join-Path $PSScriptRoot "..\Build-Solution.ps1") `
            -WorkspaceRoot $root -Platform x64 -SimdVariant sse2 -ArchFlag NotSet -PlanOnly
        $sse2.InstallerProject | Should -BeNullOrEmpty
    }

    It "derives the AVX10 and ARM64 update channels" {
        $script = Join-Path $PSScriptRoot "..\Build-QtApps.ps1"
        (& $script -WorkspaceRoot $root -Platform x64 -SimdVariant avx10_1 `
            -QtArchFlag "/arch:AVX10.1" -MsvcDevPlatform amd64 -PlanOnly).UpdateChannel |
            Should -Be "x64-avx10-1"
        (& $script -WorkspaceRoot $root -Platform ARM64 -SimdVariant neon `
            -QtArchFlag "" -MsvcDevPlatform arm64 -PlanOnly).UpdateChannel |
            Should -Be "arm64-neon"
    }

    It "pins the external DPF plugins and software OpenGL inputs" {
        $plan = & (Join-Path $PSScriptRoot "..\Build-DpfTestPlugins.ps1") `
            -WorkspaceRoot $root -PlanOnly
        @($plan.Sources | ForEach-Object Name) | Should -Be @("DPF", "DPF-Plugins")
        foreach ($source in $plan.Sources) {
            $source.Commit | Should -Match "^[0-9a-f]{40}$"
        }
        $plan.Sources[0].Submodules | Should -Be @("dgl/src/pugl-upstream")
        $plan.Sources[1].Submodules | Should -BeNullOrEmpty
        $plan.Mesa.Url | Should -Match "^https://github\.com/pal1000/mesa-dist-win/releases/download/"
        $plan.Mesa.Sha256 | Should -Match "^[0-9A-F]{64}$"
        $plan.Mesa.Files | Should -Be @("x64/opengl32.dll", "x64/libgallium_wgl.dll")
        @($plan.Plugins.Keys) | Should -Be @("EQ_VST2", "EQ_VST3", "MVERB_VST2", "MVERB_VST3")
        $plan.ManifestName | Should -Be "dpf-plugins.json"
    }

    It "plans an ASan Editor build without changing the regular Qt app set" {
        $script = Join-Path $PSScriptRoot "..\Build-QtApps.ps1"
        $arguments = @{
            WorkspaceRoot = $root
            Platform = "x64"
            SimdVariant = "avx2"
            QtArchFlag = "/arch:AVX2"
            MsvcDevPlatform = "amd64"
            PlanOnly = $true
        }
        $regular = & $script @arguments
        $regular.Apps | Should -Be @("Editor", "DeviceSelector")
        @($regular.BuildDirectories.Keys) | Should -Be @("Editor", "DeviceSelector")
        $regular.BuildDirectories.Editor | Should -Be "build-Editor-x64"
        $regular.BuildDirectories.DeviceSelector | Should -Be "build-DeviceSelector-x64"
        $regular.ExtraQmakeArguments | Should -BeNullOrEmpty

        $asan = & $script @arguments -AddressSanitizer
        $asan.Apps | Should -Be @("Editor")
        @($asan.BuildDirectories.Keys) | Should -Be @("Editor")
        $asan.BuildDirectories.Editor | Should -Be "build-Editor-x64-asan"
        $asan.ExtraQmakeArguments | Should -Be @(
            "QMAKE_CFLAGS+=/fsanitize=address",
            "QMAKE_CXXFLAGS+=/fsanitize=address",
            "DEFINES+=_DISABLE_STRING_ANNOTATION",
            "DEFINES+=_DISABLE_VECTOR_ANNOTATION",
            "QMAKE_LFLAGS+=/INCREMENTAL:NO"
        )
    }

    It "prepares a workspace in one order for every caller, from the manifest's primary variant" {
        # TECH_DEBT_AUDIT TD-35 (audit #467): the order was written in four
        # YAML places and setup-build.ps1, each a little different.
        $script = Join-Path $PSScriptRoot "..\Initialize-Workspace.ps1"
        $manifest = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "..\..\simd-variants.psd1")
        $primary = @($manifest.Variants | Where-Object { $_.Primary })[0]

        $default = & $script -WorkspaceRoot $root -PlanOnly
        $default.Simd | Should -Be $primary.Simd
        $default.Steps | Should -Be @("DownloadDependencies", "ClonePinnedSources", "InstallQt", "ExportEnvironment")
        $default.QtRoot | Should -Be (Join-Path $root "Qt\$($manifest.Shared.QtVersion)\msvc2022_64")

        (& $script -WorkspaceRoot $root -Simd sse2 -PlanOnly).Steps |
            Should -Be @("DownloadDependencies", "BuildVcpkgDependencies", "ClonePinnedSources", "InstallQt", "ExportEnvironment")
        (& $script -WorkspaceRoot $root -SkipQt -PlanOnly).Steps |
            Should -Be @("DownloadDependencies", "ClonePinnedSources", "ExportEnvironment")
        $arm = & $script -WorkspaceRoot $root -Platform ARM64 -Simd avx2 -PlanOnly
        $arm.Simd | Should -Be "neon"
        $arm.QtRoot | Should -BeLike "*msvc2022_arm64"
    }

    It "reads the arch flags and the channel from the manifest when a caller names only the variant" {
        $solution = Join-Path $PSScriptRoot "..\Build-Solution.ps1"
        $qt = Join-Path $PSScriptRoot "..\Build-QtApps.ps1"
        # No -SimdVariant at all: the primary variant, which owns the installer.
        (& $solution -WorkspaceRoot $root -Platform x64 -PlanOnly).InstallerProject | Should -Be "Installer\Installer.vcxproj"
        (& $qt -WorkspaceRoot $root -Platform x64 -PlanOnly).UpdateChannel | Should -Be "x64-avx2"
        (& $qt -WorkspaceRoot $root -Platform x64 -SimdVariant avx512 -PlanOnly).UpdateChannel | Should -Be "x64-avx512"

        Import-Module (Join-Path $PSScriptRoot "..\Provisioning.psm1") -Force
        $manifest = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "..\..\simd-variants.psd1")
        # The x64 baseline passes NotSet so a build never falls back to the
        # projects' local AVX2 default; ARM64 passes no override.
        (Resolve-BuildVariant -Manifest $manifest -Platform x64 -Simd sse2).MsBuildArchFlag | Should -Be "NotSet"
        (Resolve-BuildVariant -Manifest $manifest -Platform x64 -Simd sse2).QtArchFlag | Should -Be ""
        (Resolve-BuildVariant -Manifest $manifest -Platform ARM64 -Simd "").MsBuildArchFlag | Should -Be ""
        (Resolve-BuildVariant -Manifest $manifest -Platform x64 -Simd avx10_1).QtArchFlag | Should -Be "/arch:AVX10.1"
        { Resolve-BuildVariant -Manifest $manifest -Platform x64 -Simd nonsense } | Should -Throw

        # EditorLogicTests' fallback Qt location is the one setup-build.ps1
        # installs to (TECH_DEBT_AUDIT TD-71).
        $project = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot "..\..\..\Tests\EditorLogicTests\EditorLogicTests.vcxproj")
        $project | Should -Match ([regex]::Escape("..\..\Qt\$($manifest.Shared.QtVersion)\msvc2022_64</QT_ROOT>"))
    }

    It "plans the installer gate: update over the previous release, self-test, remove, fresh install, self-test, remove" {
        # TECH_DEBT_AUDIT TD-04 (audit #467): every Velopack hook runs on a
        # runner before a release is published.
        $script = Join-Path $PSScriptRoot "..\Invoke-InstallerGate.ps1"
        $plan = & $script -WorkspaceRoot $root -PackVersion 9.9.9 -Repository owner/repo -PlanOnly
        $plan.Channel | Should -Be "x64-avx2"
        $plan.Rounds | Should -Be @("update", "selftest", "remove", "fresh", "selftest", "remove")
        (& $script -WorkspaceRoot $root -PackVersion 9.9.9 -Repository owner/repo -SkipUpdateRound -PlanOnly).Rounds |
            Should -Be @("fresh", "selftest", "remove")

        Import-Module (Join-Path $PSScriptRoot "..\ReleaseAssets.psm1") -Force
        $plan.SetupPath | Should -Be (Join-Path $root "velopack-output\x64-avx2\$(Get-SetupAssetName -Channel 'x64-avx2')")
        $plan.InstallRoot | Should -Be (Join-Path $env:LOCALAPPDATA (Get-VelopackPackId -Channel "x64-avx2"))
        # What only a packed install has: Qt plugins under qt\, the sample
        # config, and the repair script the diagnosis points to.
        $plan.RequiredFiles | Should -Contain "qt\platforms\qwindows.dll"
        $plan.RequiredFiles | Should -Contain "config\config.txt"
        $plan.RequiredFiles | Should -Contain "tools\Repair-EqualizerAPO.ps1"
        # Both APO classes, by the CLSIDs the harness shares with the C++ headers.
        $plan.ClassKeys | Should -HaveCount 2
        $plan.ClassKeys | Should -Contain "HKLM:\SOFTWARE\Classes\CLSID\{EACD2258-FCAC-4FF4-B36D-419E924A6D79}\InprocServer32"

        # build.yml runs it as its own job and create-release waits for it.
        $workflow = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot "..\..\workflows\build.yml")
        $workflow | Should -Match "(?m)^  installer-gate:"
        $workflow | Should -Match "needs\.installer-gate\.result == 'success'"
        ([regex]::Matches($workflow, "Invoke-InstallerGate\.ps1")).Count | Should -BeGreaterOrEqual 2
    }

    It "runs the ASIO suite where the variant executes and lists the ASIO projects" {
        $avx2 = & (Join-Path $PSScriptRoot "..\Build-Solution.ps1") `
            -WorkspaceRoot $root -Platform x64 -SimdVariant avx2 -ArchFlag AdvancedVectorExtensions2 -PlanOnly
        $avx2.RuntimeTests | Should -Contain "AsioTests"
        $avx2.Projects | Should -Contain "EqualizerAPOAsio\EqualizerAPOAsio.vcxproj"
        $avx2.Projects | Should -Contain "Tests\FakeAsioDriver\FakeAsioDriver.vcxproj"
        $avx2.Projects | Should -Contain "Tests\AsioProbe\AsioProbe.vcxproj"
    }

    It "plans the ASIO probe gate over the fake driver with an in-process and a DLL shape" {
        $plan = & (Join-Path $PSScriptRoot "..\Invoke-AsioProbeGate.ps1") -WorkspaceRoot $root -Platform x64 -PlanOnly
        $plan.Probe | Should -Be (Join-Path $root "Tests\AsioProbe\x64\Release\AsioProbe.exe")
        $plan.Config | Should -Be (Join-Path $root "Tests\AsioProbe\probe-config.txt")
        @($plan.Runs | Where-Object { $_.Arguments -contains "inproc" }).Count | Should -BeGreaterThan 0
        @($plan.Runs | Where-Object { $_.Arguments -contains "passthrough" }).Count | Should -BeGreaterThan 0
        foreach ($run in $plan.Runs) {
            if ($run.Arguments -contains "inproc") {
                $run.Arguments | Should -Contain "--max-late" -Because "$($run.Name) must refuse late blocks"
            }
        }
        # The pipelined run over the real host process is timing-bound on a
        # shared runner and gets more than one attempt; the deterministic
        # runs get exactly one, so a regression in them cannot hide.
        $pipelinedDll = $plan.Runs | Where-Object { $_.Name -eq "dll-daemon-exe-pipelined-float32-96" }
        $pipelinedDll.Attempts | Should -BeGreaterThan 1
        foreach ($run in $plan.Runs) {
            if ($run.Arguments -contains "--max-late") {
                ($run.PSObject.Properties["Attempts"]) | Should -BeNullOrEmpty -Because "$($run.Name) is deterministic"
            }
        }
        # --burst is pinned exactly once, in sync mode; a regression there
        # cannot hide behind the pipelined run's timing-bound retries.
        @($plan.Runs | Where-Object { $_.Arguments -contains "--burst" }).Count | Should -Be 1
        # The paced pipelined daemon-thread run refuses every late block and
        # names the slow ones, so a failure says which block and which step.
        $pipelinedThread = $plan.Runs | Where-Object { $_.Name -eq "daemon-thread-pipelined-int24-128" }
        $pipelinedThread.Arguments | Should -Contain "--trace-slow"
        $pipelinedThread.Arguments[[array]::IndexOf($pipelinedThread.Arguments, "--max-late") + 1] | Should -Be "0"
    }

    It "runs every runtime suite and the golden regression suite under the memory gate" {
        # Audit #348 D1/TD-22: the memcheck list was typed by hand and had
        # drifted (AsioTests rebuilt with ASan but never run,
        # AudioRegressionTests absent). It now derives from Build-Solution.
        $solution = & (Join-Path $PSScriptRoot "..\Build-Solution.ps1") `
            -WorkspaceRoot $root -Platform x64 -SimdVariant avx2 -ArchFlag AdvancedVectorExtensions2 -PlanOnly
        $memcheck = & (Join-Path $PSScriptRoot "..\Invoke-MemcheckTests.ps1") -WorkspaceRoot $root -PlanOnly
        foreach ($suite in $solution.RuntimeTests) {
            $memcheck.Suites | Should -Contain $suite -Because "memcheck must run every suite Build-Solution runs"
            $memcheck.Projects | Should -Contain "Tests\$suite\$suite.vcxproj"
        }
        $memcheck.Suites | Should -Contain "AudioRegressionTests"
        $memcheck.SuiteArguments["AudioRegressionTests"] | Should -Contain "--ref-dir"
    }

    It "lists the capture probes so every leg builds them" {
        $avx2 = & (Join-Path $PSScriptRoot "..\Build-Solution.ps1") `
            -WorkspaceRoot $root -Platform x64 -SimdVariant avx2 -ArchFlag AdvancedVectorExtensions2 -PlanOnly
        $avx2.Projects | Should -Contain "Tests\ApoHostProbe\ApoHostProbe.vcxproj"
        $avx2.Projects | Should -Contain "Tests\CaptureProbe\CaptureProbe.vcxproj"
    }

    It "plans the capture gate over a pinned virtual cable with the preamp measured three ways" {
        $plan = & (Join-Path $PSScriptRoot "..\Invoke-CaptureGate.ps1") -WorkspaceRoot $root -PlanOnly
        # CI runs every measured phase; -Phases narrows a manual run.
        $plan.Phases | Should -Be @("rounds", "plugin-load", "low-latency", "asio-entry")
        (& (Join-Path $PSScriptRoot "..\Invoke-CaptureGate.ps1") -WorkspaceRoot $root -Phases plugin-load -PlanOnly).Phases |
            Should -Be @("plugin-load")
        $plan.VbCableSha256 | Should -Match "^[0-9A-F]{64}$"
        $plan.VbCableUrl | Should -Match "^https://download\.vb-audio\.com/"
        $plan.RenderConnection | Should -Be "CABLE Input"
        # Send: the receiving instance plays the sender's tone at unity.
        $plan.SendRound.ExpectGainDb | Should -Be 0
        $plan.SendRound.Required | Should -BeTrue
        $plan.CaptureConnection | Should -Be "CABLE Output"
        $plan.PreampDb | Should -BeLessThan 0
        $names = @($plan.Measurements | ForEach-Object { $_.Name })
        $names | Should -Be @("baseline", "apo-default", "apo-comms", "apo-raw", "after-uninstall")
        # The two that say "the APO processes a recording endpoint": a plain
        # recorder and a voice-chat stream, both expected at the preamp.
        foreach ($name in @("apo-default", "apo-comms")) {
            $m = $plan.Measurements | Where-Object { $_.Name -eq $name }
            $m.ExpectGainDb | Should -Be $plan.PreampDb
            $m.Required | Should -BeTrue
        }
        # Raw mode bypasses stream effects by design, so it is noted, not gated.
        ($plan.Measurements | Where-Object { $_.Name -eq "apo-raw" }).Required | Should -BeFalse
        ($plan.Measurements | Where-Object { $_.Name -eq "after-uninstall" }).ExpectGainDb | Should -Be 0
        # One install/measure/uninstall round per install mode: the one the
        # product picks on its own first, then each slot pair by name.
        @($plan.InstallModes | ForEach-Object { $_.Name }) | Should -Be @("default", "sfx-efx", "sfx-mfx", "lfx-gfx")
        ($plan.InstallModes | Where-Object { $_.Name -eq "default" }).Arguments.Count | Should -Be 0
        # The product's own choice is the gate; the named slots are the
        # evidence (a legacy driver is fed through LFX only, so a named SFX
        # round legitimately reads unity).
        ($plan.InstallModes | Where-Object { $_.Name -eq "default" }).Required | Should -BeTrue
        foreach ($named in @("sfx-efx", "sfx-mfx", "lfx-gfx")) {
            ($plan.InstallModes | Where-Object { $_.Name -eq $named }).Required | Should -BeFalse
        }
        # The low-latency round: the playback side with a convolution in the
        # config, the small period fresh and after a switch, both at the
        # preamp and both gated (the script itself skips them, and says so,
        # on a driver that declares no small period).
        @($plan.LowLatency | ForEach-Object { $_.Name }) | Should -Be @("ll-default", "ll-min", "ll-min-switch", "ll-after-uninstall")
        foreach ($name in @("ll-min", "ll-min-switch")) {
            $m = $plan.LowLatency | Where-Object { $_.Name -eq $name }
            $m.Period | Should -Be "min"
            $m.ExpectGainDb | Should -Be $plan.PreampDb
            $m.Required | Should -BeTrue
        }
        ($plan.LowLatency | Where-Object { $_.Name -eq "ll-min" }).HoldDefault | Should -BeFalse
        ($plan.LowLatency | Where-Object { $_.Name -eq "ll-min-switch" }).HoldDefault | Should -BeTrue
        ($plan.LowLatency | Where-Object { $_.Name -eq "ll-default" }).Period | Should -Be "default"
        ($plan.LowLatency | Where-Object { $_.Name -eq "ll-after-uninstall" }).ExpectGainDb | Should -Be 0
        # The ASIO entry round: the endpoint's entry opened through COM the way
        # a DAW does, the preamp expected on the far side, gated.
        $plan.AsioEntry.Name | Should -Be "asio-entry"
        $plan.AsioEntry.ExpectGainDb | Should -Be $plan.PreampDb
        $plan.AsioEntry.Required | Should -BeTrue
    }

    It "compiles every shipped C++ binary for CodeQL" {
        # Audit #348 TD-25: the CodeQL build was four projects written into the
        # YAML, and the ASIO wrapper, the engine host, the VST3 plug-in and the
        # installer shipped unscanned. Every C++ file the x64 artifact ships
        # maps to its project, which the CodeQL plan must compile.
        $repo = Join-Path $PSScriptRoot "..\..\.."
        $codeql = & (Join-Path $PSScriptRoot "..\Invoke-CodeQLBuild.ps1") -PlanOnly
        $scanned = @($codeql.Targets | ForEach-Object { "$($_.Project)|$($_.Platform)" })
        $pack = & (Join-Path $PSScriptRoot "..\Package-Artifacts.ps1") `
            -WorkspaceRoot $repo -Platform x64 -SimdVariant avx2 -PlanOnly
        $shipped = @($pack.RequiredFiles) + @($pack.Vst3PluginModule)
        foreach ($file in $shipped) {
            $projectDir = ($file -split '\\x64\\Release\\')[0]
            $projects = @(Get-ChildItem -LiteralPath (Join-Path $repo $projectDir) -Filter "*.vcxproj" -File)
            $projects.Count | Should -Be 1 -Because "$file comes from one project in $projectDir"
            $scanned | Should -Contain "$projectDir\$($projects[0].Name)|x64" -Because "$file ships"
        }
        $pack.Win32Wrapper | Should -Not -BeNullOrEmpty
        $scanned | Should -Contain "EqualizerAPOAsio\EqualizerAPOAsio.vcxproj|Win32" -Because "the x86 wrapper ships"
        $scanned | Should -Contain "Installer\Installer.vcxproj|Win32" -Because "the installer is a release asset"
    }

    It "builds only the two 32-bit ASIO DLLs for Win32" {
        $plan = & (Join-Path $PSScriptRoot "..\Build-AsioWin32.ps1") -WorkspaceRoot $root -PlanOnly
        $plan.Projects | Should -Be @("EqualizerAPOAsio\EqualizerAPOAsio.vcxproj", "Tests\FakeAsioDriver\FakeAsioDriver.vcxproj")
        $plan.BuildParams | Should -Contain "/p:Platform=Win32"
        $plan.Outputs | Should -Contain (Join-Path $root "EqualizerAPOAsio\Release\EqualizerAPOAsio.dll")
    }

    It "keeps symbols and object files out of user artifacts" {
        $plan = & (Join-Path $PSScriptRoot "..\Package-Artifacts.ps1") `
            -WorkspaceRoot $root -Platform x64 -SimdVariant avx2 -PlanOnly
        $plan.RequiredFiles | Should -Contain "EqualizerAPO\x64\Release\EqualizerAPO.dll"
        # Editor.exe --diagnose sends the user to this script; it has to be in
        # the installed folder (audit #467, maintainer question 11).
        $plan.SupportScripts | Should -Contain "tools\Repair-EqualizerAPO.ps1"
        foreach ($script in $plan.SupportScripts) {
            Join-Path $PSScriptRoot "..\..\..\$script" | Should -Exist
        }
        $plan.ExcludedExtensions | Should -Contain ".pdb"
        $plan.ExcludedExtensions | Should -Contain ".obj"
    }

    It "keeps the Qt build's precompiled headers and generated sources out of user artifacts" {
        # v2.38.0 to v2.48.0 shipped them: 887 MB of .pch and 119 MB of
        # moc_/qrc_ sources unpacked, a ~250 MB installer instead of ~65 MB.
        $plan = & (Join-Path $PSScriptRoot "..\Package-Artifacts.ps1") `
            -WorkspaceRoot $root -Platform x64 -SimdVariant avx2 -PlanOnly
        foreach ($extension in @(".pch", ".cpp", ".h")) {
            $plan.ExcludedExtensions | Should -Contain $extension
        }
    }

    It "keeps the symbols of every program the artifact ships" {
        # Audit #348 TD-75: the inline step kept three PDBs of nine. The list
        # now comes from the packaging plan, so every shipped program has one.
        foreach ($platform in @("x64", "ARM64")) {
            $package = & (Join-Path $PSScriptRoot "..\Package-Artifacts.ps1") `
                -WorkspaceRoot $root -Platform $platform -SimdVariant avx2 -PlanOnly
            $symbols = @(& (Join-Path $PSScriptRoot "..\Collect-Symbols.ps1") `
                -WorkspaceRoot $root -Platform $platform -SimdVariant avx2 -PlanOnly)
            $sources = @($symbols | ForEach-Object { $_.Source })
            foreach ($binary in @($package.RequiredFiles) + @($package.Vst3PluginModule)) {
                $sources | Should -Contain ([System.IO.Path]::ChangeExtension($binary, ".pdb"))
            }
            foreach ($app in $package.QtApps) {
                $sources | Should -Contain "build-$app-$platform\release\$app.pdb"
            }
            $sources | Should -Contain "EqualizerAPOHost\$platform\Release\EqualizerAPOHost.pdb"
            # Two programs share a file name only across x64 and x86; no
            # destination may be written twice.
            @($symbols | ForEach-Object { $_.Destination } | Sort-Object -Unique).Count | Should -Be $symbols.Count
        }
        $x64 = @(& (Join-Path $PSScriptRoot "..\Collect-Symbols.ps1") -WorkspaceRoot $root -Platform x64 -SimdVariant avx2 -PlanOnly)
        ($x64 | Where-Object { $_.Source -eq "EqualizerAPOAsio\Release\EqualizerAPOAsio.pdb" }).Destination |
            Should -Be "x86\EqualizerAPOAsio.pdb"
        $arm64 = @(& (Join-Path $PSScriptRoot "..\Collect-Symbols.ps1") -WorkspaceRoot $root -Platform ARM64 -SimdVariant neon -PlanOnly)
        @($arm64 | Where-Object { $_.Destination -like "x86\*" }).Count | Should -Be 0
    }

    It "fails when a shipped program left no symbols" {
        $fixture = Join-Path $TestDrive "symbols-fixture"
        New-Item -ItemType Directory -Force -Path $fixture | Out-Null
        { & (Join-Path $PSScriptRoot "..\Collect-Symbols.ps1") -WorkspaceRoot $fixture -Platform x64 -SimdVariant avx2 `
            -SymbolsDirectory (Join-Path $fixture "symbols") 6>$null } | Should -Throw "*left no debug symbols*"
    }

    It "verifies against the committed references and records only on request" {
        # Audit #348 TD-75 moved the decision out of build.yml; the rules are
        # audit #275 TD-08's: never seed a missing set implicitly.
        $suite = Join-Path $TestDrive "art\Tests\AudioRegressionTests\references"
        New-Item -ItemType Directory -Force -Path $suite | Out-Null
        $workspace = Join-Path $TestDrive "art"
        $script = Join-Path $PSScriptRoot "..\Invoke-AudioRegression.ps1"

        $missing = & $script -WorkspaceRoot $workspace -Platform x64 -SimdVariant avx2 -Primary -PlanOnly
        $missing.Mode | Should -Be "MissingReferences"

        Set-Content -LiteralPath (Join-Path $suite "cases.json") -Value "{}"
        $verify = & $script -WorkspaceRoot $workspace -Platform x64 -SimdVariant avx2 -Primary -PlanOnly
        $verify.Mode | Should -Be "Verify"
        $verify.Arguments | Should -Not -Contain "--generate-references"
        $verify.Arguments | Should -Contain "avx2"

        $record = & $script -WorkspaceRoot $workspace -Platform x64 -SimdVariant avx2 -Primary -Regenerate -PlanOnly
        $record.Mode | Should -Be "RecordReferences"
        $record.Arguments | Should -Contain "--generate-references"

        $variant = & $script -WorkspaceRoot $workspace -Platform x64 -SimdVariant sse2 -Regenerate -PlanOnly
        $variant.Mode | Should -Be "RecordVariant"
    }
}

Describe "BuildModel.psm1" {
    BeforeAll {
        Import-Module (Join-Path $PSScriptRoot "..\BuildModel.psm1") -Force
        $script:root = Join-Path $PSScriptRoot "..\..\.."
    }

    It "lists every project once, and every listed project exists" {
        $graph = @(Get-EapoProjectGraph)
        ($graph.Path | Select-Object -Unique).Count | Should -Be $graph.Count
        foreach ($project in $graph) {
            Join-Path $script:root $project.Path | Should -Exist
        }
    }

    It "orders each project after the projects it references" {
        $graph = @(Get-EapoProjectGraph)
        $position = @{}
        for ($i = 0; $i -lt $graph.Count; $i++) { $position[$graph[$i].Path] = $i }
        foreach ($project in $graph) {
            $file = Join-Path $script:root $project.Path
            $xml = [xml](Get-Content -LiteralPath $file -Raw)
            $refs = @($xml.Project.ItemGroup.ProjectReference | Where-Object { $_ } | ForEach-Object { $_.Include })
            foreach ($ref in $refs) {
                $full = [System.IO.Path]::GetFullPath((Join-Path (Split-Path $file -Parent) $ref))
                $relative = [System.IO.Path]::GetRelativePath([System.IO.Path]::GetFullPath($script:root), $full)
                if ($position.ContainsKey($relative)) {
                    $position[$relative] | Should -BeLessThan $position[$project.Path] -Because "$($project.Name) references $relative"
                }
            }
        }
    }

    It "selects by tag in graph order" {
        @(Get-EapoProjectGraph -Tag engine).Name | Should -Be @("SubwooferRoutingCore", "Common")
        @(Get-EapoProjectGraph -Tag win32).Name | Should -Be @("EqualizerAPOAsio", "FakeAsioDriver")
        @(Get-EapoProjectGraph -Tag installer).Path | Should -Be @("Installer\Installer.vcxproj")
    }

    It "passes a dependency path only when its variable is set" {
        $saved = $env:VST3_SDK
        try {
            $env:VST3_SDK = ""
            Get-EapoMsBuildArguments -Platform x64 | Where-Object { $_ -like "/p:VST3_SDK=*" } | Should -BeNullOrEmpty
            $env:VST3_SDK = "C:\deps\vst3sdk"
            Get-EapoMsBuildArguments -Platform x64 | Should -Contain "/p:VST3_SDK=C:\deps\vst3sdk"
            Get-EapoMsBuildArguments -Platform x64 -Dependencies None | Where-Object { $_ -like "/p:VST3_SDK=*" } | Should -BeNullOrEmpty
        } finally { $env:VST3_SDK = $saved }
    }

    It "adds the arch flag on x64 only" {
        Get-EapoMsBuildArguments -Platform x64 -ArchFlag AdvancedVectorExtensions2 |
            Should -Contain "/p:EnableEnhancedInstructionSet=AdvancedVectorExtensions2"
        Get-EapoMsBuildArguments -Platform ARM64 -ArchFlag AdvancedVectorExtensions2 |
            Where-Object { $_ -like "/p:EnableEnhancedInstructionSet=*" } | Should -BeNullOrEmpty
    }
}
