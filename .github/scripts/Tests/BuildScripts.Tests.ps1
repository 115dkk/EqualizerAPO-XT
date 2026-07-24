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

    It "keeps symbols and object files out of user artifacts" {
        $plan = & (Join-Path $PSScriptRoot "..\Package-Artifacts.ps1") `
            -WorkspaceRoot $root -Platform x64 -SimdVariant avx2 -PlanOnly
        $plan.RequiredFiles | Should -Contain "EqualizerAPO\x64\Release\EqualizerAPO.dll"
        $plan.ExcludedExtensions | Should -Contain ".pdb"
        $plan.ExcludedExtensions | Should -Contain ".obj"
    }
}
