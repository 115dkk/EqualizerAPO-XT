Describe "New-VelopackRelease.ps1 planning" {
    BeforeAll {
        $scriptPath = Join-Path $PSScriptRoot "..\New-VelopackRelease.ps1"
        $manifestPath = Join-Path $PSScriptRoot "..\..\simd-variants.psd1"

        function NewInputRoot([string[]] $artifactNames) {
            $root = Join-Path $TestDrive ([guid]::NewGuid().ToString("N"))
            $inputRoot = Join-Path $root "release-input"
            New-Item -ItemType Directory -Force -Path $inputRoot | Out-Null
            foreach ($name in $artifactNames) {
                New-Item -ItemType Directory -Force -Path (Join-Path $inputRoot $name) | Out-Null
            }
            @{ Workspace = $root; InputRoot = $inputRoot }
        }

        function PlanFor([hashtable] $tree, [string[]] $missing, [switch] $needsSource) {
            & $scriptPath -Repository owner/repo -Tag v9.9.9 -PackVersion 9.9.9 `
                -TargetCommit abc123 -WorkspaceRoot $tree.Workspace `
                -InputRoot $tree.InputRoot -MissingChannels $missing `
                -NeedsSource:$needsSource -ManifestPath $manifestPath -PlanOnly
        }
    }

    It "resolves channels from the manifest, not a naming convention" {
        $tree = NewInputRoot @("EqualizerAPO-x64-avx10_1", "EqualizerAPO-ARM64-neon")
        $plan = PlanFor $tree @("x64-avx10-1", "arm64-neon")
        $plan.Channels | Should -HaveCount 2
        # avx10_1 folds to the avx10-1 channel and arm64 keeps its own; both
        # are exactly what a "x64- + simd" string convention would get wrong.
        ($plan.Channels | Where-Object Variant -eq "x64-avx10_1").Channel | Should -Be "x64-avx10-1"
        ($plan.Channels | Where-Object Variant -eq "ARM64-neon").Channel | Should -Be "arm64-neon"
        ($plan.Channels | Where-Object Variant -eq "ARM64-neon").Framework | Should -Be "vcredist143-arm64"
        ($plan.Channels | Where-Object Variant -eq "x64-avx10_1").Framework | Should -Be "vcredist143-x64"
    }

    It "packs under the grammar module's pack id" {
        $tree = NewInputRoot @("EqualizerAPO-x64-avx2")
        $plan = PlanFor $tree @("x64-avx2")
        $plan.Channels[0].PackId | Should -Be "EqualizerAPO-XT-x64-avx2"
        $plan.ReleaseName | Should -Be "EqualizerAPO-XT 9.9.9"
    }

    It "skips channels the release already carries (resume)" {
        $tree = NewInputRoot @("EqualizerAPO-x64-avx2", "EqualizerAPO-x64-sse2")
        $plan = PlanFor $tree @("x64-sse2")
        ($plan.Channels | Where-Object Channel -eq "x64-avx2").Skipped | Should -BeTrue
        ($plan.Channels | Where-Object Channel -eq "x64-sse2").Skipped | Should -BeFalse
    }

    It "carries the source zip decision and its grammar name" {
        $tree = NewInputRoot @("EqualizerAPO-x64-avx2")
        $plan = PlanFor $tree @("x64-avx2") -needsSource
        $plan.NeedsSource | Should -BeTrue
        Split-Path $plan.SourceZipPath -Leaf | Should -Be "EqualizerAPO-XT-source-9.9.9.zip"
    }

    It "fails the plan on an artifact no manifest variant claims" {
        $tree = NewInputRoot @("EqualizerAPO-x64-quantum")
        { PlanFor $tree @() } | Should -Throw "*no matching variant*"
    }

    It "fails the plan when no artifacts arrived" {
        $tree = NewInputRoot @()
        { PlanFor $tree @() } | Should -Throw "*No build artifacts*"
    }
}
