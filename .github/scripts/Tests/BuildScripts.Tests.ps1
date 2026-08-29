Describe "extracted build script decisions" {
    BeforeAll {
        $root = Join-Path $TestDrive "repo"
    }

    It "selects native ARM64 toolchain and suppresses unsupported runtime tests" {
        $plan = & (Join-Path $PSScriptRoot "..\Build-Solution.ps1") `
            -WorkspaceRoot $root -Platform ARM64 -SimdVariant neon -CanExecute:$false -PlanOnly
        $plan.PlatformToolset | Should -Be "v143"
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
        $pipelinedDll = $plan.Runs | Where-Object { $_.Name -eq "dll-daemon-exe-pipelined-float32-32" }
        $pipelinedDll.Attempts | Should -BeGreaterThan 1
        foreach ($run in $plan.Runs) {
            if ($run.Arguments -contains "--max-late") {
                ($run.PSObject.Properties["Attempts"]) | Should -BeNullOrEmpty -Because "$($run.Name) is deterministic"
            }
        }
    }

    It "lists the capture probes so every leg builds them" {
        $avx2 = & (Join-Path $PSScriptRoot "..\Build-Solution.ps1") `
            -WorkspaceRoot $root -Platform x64 -SimdVariant avx2 -ArchFlag AdvancedVectorExtensions2 -PlanOnly
        $avx2.Projects | Should -Contain "Tests\ApoHostProbe\ApoHostProbe.vcxproj"
        $avx2.Projects | Should -Contain "Tests\CaptureProbe\CaptureProbe.vcxproj"
    }

    It "plans the capture gate over a pinned virtual cable with the preamp measured three ways" {
        $plan = & (Join-Path $PSScriptRoot "..\Invoke-CaptureGate.ps1") -WorkspaceRoot $root -PlanOnly
        $plan.VbCableSha256 | Should -Match "^[0-9A-F]{64}$"
        $plan.VbCableUrl | Should -Match "^https://download\.vb-audio\.com/"
        $plan.RenderConnection | Should -Be "CABLE Input"
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
}
