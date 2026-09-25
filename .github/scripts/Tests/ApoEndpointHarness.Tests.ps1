Describe "ApoEndpointHarness" {
    BeforeAll {
        Import-Module (Join-Path $PSScriptRoot "..\ApoEndpointHarness.psm1") -Force
    }

    It "owns the five FxProperties value names in install order" {
        Get-ApoFxValueNames | Should -Be @(
            "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1"
            "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2"
            "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5"
            "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6"
            "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7"
        )
    }

    It "exports the C++ registry and APO vocabulary to GitHub Actions" {
        $environment = Join-Path $TestDrive "github-env"
        Export-ApoEndpointContract -GitHubEnvironmentPath $environment
        $text = Get-Content $environment -Raw
        $text | Should -Match "EQ_PREMIX_GUID=\{EACD2258-"
        $text | Should -Match "DEVICE_FRIENDLY_VALUE=\{b3f8fa53-"
        $text | Should -Match "MMDEVICES_ROOT=HKLM\\SOFTWARE\\Microsoft"
        (Get-Content $environment) | Should -HaveCount 10
    }

    It "names the slots by their short names in install order" {
        $slots = Get-ApoFxSlots
        @($slots.Keys) | Should -Be @("LFX", "GFX", "SFX", "MFX", "EFX")
        @($slots.Values) | Should -Be (Get-ApoFxValueNames)
    }

    It "formats an effect chain from FxProperties values, leaving empty slots out" {
        $fx = [pscustomobject]@{
            "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1" = "{EACD2258-FCAC-4FF4-B36D-419E924A6D79}"
            "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2" = ""
            "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6" = "{11111111-2222-3333-4444-555555555506}"
            "{b3f8fa53-0004-438e-9003-51a46e139bfc},6" = "Speakers"
        }
        Format-ApoEffectChain -FxProperties $fx |
            Should -Be "LFX={EACD2258-FCAC-4FF4-B36D-419E924A6D79} MFX={11111111-2222-3333-4444-555555555506}"
        Format-ApoEffectChain -FxProperties $null | Should -BeNullOrEmpty
    }

    It "finds an EQ APO CLSID left in any FxProperties value" {
        $left = [pscustomobject]@{ "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2" = "{EC1CC9CE-FAED-4822-828A-82A81A6F018F}" }
        $clean = [pscustomobject]@{ "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2" = "{11111111-2222-3333-4444-555555555502}" }
        Test-ApoEqClsid -FxProperties $left | Should -BeTrue
        Test-ApoEqClsid -FxProperties $clean | Should -BeFalse
        Test-ApoEqClsid -FxProperties $null | Should -BeFalse
    }
}
