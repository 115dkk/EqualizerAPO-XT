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
}
