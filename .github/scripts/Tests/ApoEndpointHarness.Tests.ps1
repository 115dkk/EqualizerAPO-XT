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
}
