Describe "New-ReleaseNotes.ps1" {
    BeforeAll {
        $scriptPath = Join-Path $PSScriptRoot "..\New-ReleaseNotes.ps1"
        $manifestPath = Join-Path $PSScriptRoot "..\..\simd-variants.psd1"
    }

    BeforeEach {
        $global:ReleaseNotesGhMode = "compare"
        function global:gh {
            $joined = $args -join " "
            $global:LASTEXITCODE = 0
            if ($joined -match "releases/tags/") {
                $channels = (Import-PowerShellDataFile $manifestPath).Variants.Channel
                $assets = @(
                    @{ name = "EqualizerAPO-XT-Setup.exe"; size = 10; browser_download_url = "https://example/universal" }
                    @{ name = "EqualizerAPO-XT-source-9.9.9.zip"; size = 10; browser_download_url = "https://example/source" }
                )
                foreach ($channel in $channels) {
                    $assets += @{ name = "EqualizerAPO-XT-$channel-$channel-Setup.exe"; size = 10; browser_download_url = "https://example/$channel" }
                    $assets += @{ name = "releases.$channel.json"; size = 10; browser_download_url = "https://example/$channel-feed" }
                }
                # Prefix neighbours: exact/prefix classification must not let
                # x64-avx claim the x64-avx2 nupkg or vice versa (audit #275).
                $assets += @{ name = "EqualizerAPO-XT-x64-avx-9.9.9-full.nupkg"; size = 10; browser_download_url = "https://example/avx-full" }
                $assets += @{ name = "EqualizerAPO-XT-x64-avx2-9.9.9-full.nupkg"; size = 10; browser_download_url = "https://example/avx2-full" }
                return (@{ assets = $assets } | ConvertTo-Json -Depth 5 -Compress)
            }
            if ($joined.Contains("releases?per_page")) {
                if ($global:ReleaseNotesGhMode -eq "no-previous") {
                    return "[]"
                }
                return (@(@{ tag_name = "v9.9.8"; draft = $false; html_url = "https://example/previous" }) |
                    ConvertTo-Json -Depth 5 -Compress)
            }
            if ($joined -match "/compare/") {
                if ($global:ReleaseNotesGhMode -eq "compare-unavailable") {
                    $global:LASTEXITCODE = 1
                    return ""
                }
                return (@{
                    html_url = "https://example/compare"
                    commits = @(@{
                        sha = "abcdef0123456789"
                        html_url = "https://example/commit"
                        commit = @{ message = "A tested change" }
                    })
                } | ConvertTo-Json -Depth 5 -Compress)
            }
            throw "Unexpected gh invocation: $joined"
        }
    }

    AfterEach {
        Remove-Item Function:\global:gh -ErrorAction SilentlyContinue
        Remove-Variable ReleaseNotesGhMode -Scope Global -ErrorAction SilentlyContinue
    }

    It "classifies every manifest channel and gives avx10 precedence over avx" {
        $output = Join-Path $TestDrive "notes.md"
        . $scriptPath -Repository "owner/repo" -Tag "v9.9.9" -PackVersion "9.9.9" `
            -WorkflowRunId "123" -TargetCommit "abcdef0123456789" -OutputPath $output
        $notes = Get-Content $output -Raw
        foreach ($channel in (Import-PowerShellDataFile $manifestPath).Variants.Channel) {
            $notes | Should -Match ([regex]::Escape("Manual installer for the $channel channel."))
            $notes | Should -Match ([regex]::Escape("Velopack update feed for the $channel channel."))
        }
        $notes | Should -Match ([regex]::Escape("Velopack full package for the x64-avx channel."))
        $notes | Should -Match ([regex]::Escape("Velopack full package for the x64-avx2 channel."))
        $notes | Should -Match "x64-avx10-1-x64-avx10-1-Setup.exe"
        $notes | Should -Match "Changes since"
        $notes | Should -Match "A tested change"
    }

    It "falls back to the current commit when compare is unavailable" {
        $global:ReleaseNotesGhMode = "compare-unavailable"
        $output = Join-Path $TestDrive "notes-no-compare.md"
        . $scriptPath -Repository "owner/repo" -Tag "v9.9.9" -PackVersion "9.9.9" `
            -WorkflowRunId "123" -TargetCommit "abcdef0123456789" -OutputPath $output
        (Get-Content $output -Raw) | Should -Match "compare API was not available"
    }

    It "handles a first release with no previous release" {
        $global:ReleaseNotesGhMode = "no-previous"
        $output = Join-Path $TestDrive "notes-first.md"
        . $scriptPath -Repository "owner/repo" -Tag "v9.9.9" -PackVersion "9.9.9" `
            -WorkflowRunId "123" -TargetCommit "abcdef0123456789" -OutputPath $output
        (Get-Content $output -Raw) | Should -Match "No previous GitHub Release was found"
    }
}
