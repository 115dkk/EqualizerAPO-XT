Describe "ReleaseAssets grammar module" {
    BeforeAll {
        Import-Module (Join-Path $PSScriptRoot "..\ReleaseAssets.psm1") -Force
        $headerPath = Join-Path $PSScriptRoot "..\..\..\release\ReleaseAssetNames.h"
    }

    It "spells the per-channel setup asset with the doubled channel" {
        Get-SetupAssetName -Channel "x64-avx2" |
            Should -Be "EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe"
        Get-SetupAssetName -Channel "arm64-neon" |
            Should -Be "EqualizerAPO-XT-arm64-neon-arm64-neon-Setup.exe"
    }

    It "spells the companion assets" {
        Get-UniversalSetupAssetName | Should -Be "EqualizerAPO-XT-Setup.exe"
        Get-FeedAssetName -Channel "arm64-neon" | Should -Be "releases.arm64-neon.json"
        Get-SourceZipAssetName -PackVersion "2.35.0" | Should -Be "EqualizerAPO-XT-source-2.35.0.zip"
        Get-ChecksumsAssetName | Should -Be "SHA256SUMS.txt"
        Get-VelopackPackId -Channel "x64-sse2" | Should -Be "EqualizerAPO-XT-x64-sse2"
    }

    It "stays in step with the C++ grammar header" {
        # The two languages cannot share code, so this asserts the header's
        # literals and shape against the module's output. A grammar change
        # that lands in only one language fails here.
        $header = Get-Content $headerPath -Raw
        $header | Should -Match 'productPrefix\[\] = L"EqualizerAPO-XT";'
        $header | Should -Match 'checksumsAssetName\[\] = L"SHA256SUMS\.txt";'
        $header | Should -Match ([regex]::Escape('velopackPackId(channel) + L"-" + channel + L"-Setup.exe"'))
        $header | Should -Match ([regex]::Escape('std::wstring(productPrefix) + L"-" + channel'))
        $header | Should -Match ([regex]::Escape('std::wstring(productPrefix) + L"-Setup.exe"'))
    }

    It "stays in step with the installer project's TargetName and resource metadata" {
        # Audit #275 TD-14: the universal installer name is also spelled by the
        # MSBuild TargetName and the .rc version block. build.yml derives its
        # path from the grammar module, so these two are the remaining
        # spellings; a TargetName change must fail here, not on release day.
        $expected = Get-UniversalSetupAssetName
        $expectedBase = [System.IO.Path]::GetFileNameWithoutExtension($expected)

        $vcxproj = Get-Content (Join-Path $PSScriptRoot "..\..\..\Installer\Installer.vcxproj") -Raw
        $vcxproj -match '<TargetName>([^<]+)</TargetName>' | Should -BeTrue
        $Matches[1] | Should -Be $expectedBase

        $rc = Get-Content (Join-Path $PSScriptRoot "..\..\..\Installer\AutoInstaller.rc") -Raw
        $rc -match '"OriginalFilename",\s*"([^"]+)"' | Should -BeTrue
        $Matches[1] | Should -Be $expected
    }

    It "yields well-formed names for every manifest channel" {
        $manifest = Import-PowerShellDataFile (Join-Path $PSScriptRoot "..\..\simd-variants.psd1")
        foreach ($channel in @($manifest.Variants.Channel)) {
            Get-SetupAssetName -Channel $channel |
                Should -Match "^EqualizerAPO-XT-$([regex]::Escape($channel))-$([regex]::Escape($channel))-Setup\.exe$"
        }
    }

    Context "previous-release selection for installer reuse" {
        It "skips the release being assembled and anything unpublished" {
            $releases = @(
                [pscustomobject]@{ tagName = "v2.38.0"; isDraft = $false; isPrerelease = $false }
                [pscustomobject]@{ tagName = "v2.37.9"; isDraft = $true; isPrerelease = $false }
                [pscustomobject]@{ tagName = "v2.37.8"; isDraft = $false; isPrerelease = $true }
                [pscustomobject]@{ tagName = "v2.37.3"; isDraft = $false; isPrerelease = $false }
            )
            Select-PreviousReleaseTag -Releases $releases -CurrentTag "v2.38.0" |
                Should -Be "v2.37.3"
        }

        It "returns nothing when the only published release is the current one" {
            $releases = @(
                [pscustomobject]@{ tagName = "v1.0.0"; isDraft = $false; isPrerelease = $false }
            )
            Select-PreviousReleaseTag -Releases $releases -CurrentTag "v1.0.0" |
                Should -BeNullOrEmpty
            Select-PreviousReleaseTag -Releases @() -CurrentTag "v1.0.0" |
                Should -BeNullOrEmpty
        }
    }
}
