Describe "Publish-Release.ps1 resume planning" {
    BeforeAll {
        $scriptPath = Join-Path $PSScriptRoot "..\Publish-Release.ps1"
        $manifestPath = Join-Path $PSScriptRoot "..\..\simd-variants.psd1"
        $channels = @((Import-PowerShellDataFile $manifestPath).Variants.Channel)
        function CompleteAssets([string] $version) {
            $assets = @(
                "EqualizerAPO-XT-Setup.exe"
                "EqualizerAPO-XT-source-$version.zip"
                "SHA256SUMS.txt"
            )
            foreach ($channel in $channels) {
                $assets += "EqualizerAPO-XT-$channel-$channel-Setup.exe"
                $assets += "releases.$channel.json"
            }
            $assets
        }
    }

    It "treats checksums plus notes and every channel as complete" {
        $plan = & $scriptPath -Repository owner/repo -Tag v9.9.9 -PackVersion 9.9.9 `
            -ManifestPath $manifestPath -AssetNames (CompleteAssets 9.9.9) `
            -ReleaseNotes "release notes" -PassThru
        $plan.Complete | Should -BeTrue
        $plan.MissingChannels | Should -HaveCount 0
        $plan.NeedsChecksums | Should -BeFalse
    }

    It "resumes only the missing channel from a partial release" {
        $assets = @(CompleteAssets 9.9.9)
        $missing = $channels[2]
        $assets = @($assets | Where-Object {
            $_ -ne "EqualizerAPO-XT-$missing-$missing-Setup.exe" -and
            $_ -ne "releases.$missing.json"
        })
        $plan = & $scriptPath -Repository owner/repo -Tag v9.9.9 -PackVersion 9.9.9 `
            -ManifestPath $manifestPath -AssetNames $assets `
            -ReleaseNotes "old notes" -PassThru
        $plan.Complete | Should -BeFalse
        $plan.MissingChannels | Should -Be @($missing)
        $plan.NeedsChecksums | Should -BeTrue
        $plan.NeedsNotes | Should -BeTrue
    }

    It "does not accept an asset-complete release with empty notes" {
        $plan = & $scriptPath -Repository owner/repo -Tag v9.9.9 -PackVersion 9.9.9 `
            -ManifestPath $manifestPath -AssetNames (CompleteAssets 9.9.9) `
            -ReleaseNotes "" -PassThru
        $plan.Complete | Should -BeFalse
        $plan.NeedsNotes | Should -BeTrue
    }

    It "plans every phase for a new release" {
        $plan = & $scriptPath -Repository owner/repo -Tag v9.9.9 -PackVersion 9.9.9 `
            -ManifestPath $manifestPath -AssetNames @() -ReleaseNotes "" -PassThru
        $plan.MissingChannels | Should -HaveCount $channels.Count
        $plan.NeedsSource | Should -BeTrue
        $plan.NeedsInstaller | Should -BeTrue
        $plan.NeedsChecksums | Should -BeTrue
        $plan.NeedsNotes | Should -BeTrue
    }
}
