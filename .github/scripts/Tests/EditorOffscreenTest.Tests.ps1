Describe "Invoke-EditorOffscreenTest.ps1 planning" {
    BeforeAll {
        $scriptPath = Join-Path $PSScriptRoot "..\Invoke-EditorOffscreenTest.ps1"
        function PlanFor([string] $gate) {
            & $scriptPath -WorkspaceRoot "C:\ws" -Gate $gate -Platform x64 -PlanOnly
        }
    }

    It "targets the qmake build directory's Editor" {
        (PlanFor "selftest-vst").EditorExe | Should -Be "C:\ws\build-Editor-x64\release\Editor.exe"
    }

    It "copies the velopack SONAME for every gate" {
        foreach ($gate in @("selftest-vst", "skin-gallery", "skin-switch", "analysis-layout", "card-move", "card-selection", "power-toggle", "routing-edit")) {
            (PlanFor $gate).VelopackDllSource |
                Should -Be "C:\ws\deps\velopack_libc\lib\velopack_libc_win_x64_msvc.dll"
        }
    }

    It "hands the gallery its deterministic VST fixtures and the empty-run check" {
        $plan = PlanFor "skin-gallery"
        $plan.Arguments | Should -Be @("--skin-gallery", "C:\ws\skin-gallery")
        $plan.ExtraEnv["EAPO_GALLERY_VST3_PLUGIN"] |
            Should -Be "C:\ws\Tests\TestVst3Plugin\x64\Release\TestVst3Plugin.vst3"
        $plan.ExtraEnv["EAPO_GALLERY_VST2_PLUGIN"] |
            Should -Be "C:\ws\Tests\TestVst2Plugin\x64\Release\TestVst2Plugin.dll"
        $plan.PostChecks | Should -Contain "gallery-not-empty"
    }

    It "keeps the switch and move latency budgets" {
        $switch = PlanFor "skin-switch"
        $switch.ExtraEnv["EAPO_SWITCH_WARN_MS"] | Should -Be "2500"
        $switch.ExtraEnv["EAPO_SWITCH_LIMIT_MS"] | Should -Be "5000"
        $move = PlanFor "card-move"
        $move.ExtraEnv["EAPO_MOVE_WARN_MS"] | Should -Be "250"
        $move.ExtraEnv["EAPO_MOVE_LIMIT_MS"] | Should -Be "1000"
    }

    It "drives the analysis probe with the shipped sample config and demands the screenshot" {
        $plan = PlanFor "analysis-layout"
        $plan.Arguments[1] | Should -Be "C:\ws\analysis-layout\right-dock.png"
        $plan.Arguments[2] | Should -Be "C:\ws\Setup\config\config.txt"
        $plan.PostChecks | Should -Contain "analysis-screenshot"
    }

    It "captures stderr for the GUI-subsystem gates that log through qWarning" {
        foreach ($gate in @("skin-switch", "analysis-layout", "card-move", "card-selection", "power-toggle", "routing-edit")) {
            $plan = PlanFor $gate
            $plan.LogPath | Should -Not -BeNullOrEmpty
            $plan.ExtraEnv["QT_FORCE_STDERR_LOGGING"] | Should -Be "1"
        }
    }
}

Describe "New-ReleaseChecksums.ps1" {
    BeforeAll {
        $scriptPath = Join-Path $PSScriptRoot "..\New-ReleaseChecksums.ps1"
        # Dot-source in a scriptblock cannot reach an advanced script's inner
        # functions; instead run -PlanOnly for the surface, and reproduce the
        # writer contract through a real invocation of Format-ChecksumLines by
        # loading the script text's function into scope.
        $scriptText = Get-Content $scriptPath -Raw
        $functionText = [regex]::Match($scriptText,
            'function Format-ChecksumLines \{[\s\S]*?\n\}').Value
        Invoke-Expression $functionText
    }

    It "plans the grammar-derived checksum asset" {
        $plan = & $scriptPath -Repository owner/repo -Tag v9.9.9 -WorkspaceRoot "C:\ws" -PlanOnly
        $plan.ChecksumsAssetName | Should -Be "SHA256SUMS.txt"
        $plan.SumsPath | Should -Be "C:\ws\SHA256SUMS.txt"
    }

    It "writes the exact format Installer/AutoInstaller.cpp parses" {
        # sha256sum-compatible: lowercase hex, two spaces, LF, sorted, final LF.
        $lines = Format-ChecksumLines -Hashes @{
            "B-Setup.exe" = "ABCDEF0123"
            "A-Setup.exe" = "0123ABCDEF"
        }
        $lines | Should -Be "0123abcdef  A-Setup.exe`nabcdef0123  B-Setup.exe`n"
        $lines.Contains("`r") | Should -BeFalse
    }
}
