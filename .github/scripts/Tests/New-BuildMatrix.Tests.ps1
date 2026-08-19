Describe "New-BuildMatrix.ps1" {
    # Audit #275 TD-33: the PR filter and the sse2 -> NotSet arch expansion
    # decide what a pull request gates on and which /arch every CI compile
    # gets, but neither decision had a test.
    BeforeAll {
        $scriptPath = Join-Path $PSScriptRoot "..\New-BuildMatrix.ps1"
        $manifestPath = Join-Path $PSScriptRoot "..\..\simd-variants.psd1"
        $manifest = Import-PowerShellDataFile $manifestPath
        # Keep the script's GITHUB_OUTPUT writes out of the real step output
        # when these tests run inside an Actions job.
        $script:savedGithubOutput = $env:GITHUB_OUTPUT
        $env:GITHUB_OUTPUT = $null
    }

    AfterAll {
        $env:GITHUB_OUTPUT = $script:savedGithubOutput
    }

    It "expands every manifest variant on push" {
        $result = & $scriptPath -EventName push -PassThru
        @($result.Matrix.include).Count | Should -Be @($manifest.Variants).Count
        @($result.Matrix.name) | Should -Be @($manifest.Variants.Name)
    }

    It "builds only the primary variant on pull requests" {
        $result = & $scriptPath -EventName pull_request -PassThru
        $included = @($result.Matrix.include)
        $included.Count | Should -Be 1
        $included[0].primary | Should -BeTrue
        $primaryName = @($manifest.Variants | Where-Object { $_.Primary })[0].Name
        $included[0].name | Should -Be $primaryName
    }

    It "expands a missing x64 ArchFlag to the explicit NotSet sentinel" {
        # sse2 must never fall back to the project files' local AVX2 default.
        $result = & $scriptPath -EventName push -PassThru
        $x64Entries = @($result.Matrix.include | Where-Object { $_.platform -eq 'x64' })
        $x64Entries.Count | Should -BeGreaterThan 0
        foreach ($entry in $x64Entries) {
            $entry.arch_flag | Should -Not -BeNullOrEmpty
        }
        $sse2 = @($x64Entries | Where-Object { $_.simd_variant -eq 'sse2' })[0]
        $sse2.arch_flag | Should -Be 'NotSet'
    }

    It "gives ARM64 entries no arch_flag key" {
        $result = & $scriptPath -EventName push -PassThru
        $arm = @($result.Matrix.include | Where-Object { $_.platform -eq 'ARM64' })
        $arm.Count | Should -BeGreaterThan 0
        foreach ($entry in $arm) {
            $entry.PSObject.Properties.Name | Should -Not -Contain 'arch_flag'
        }
    }

    It "carries the manifest channel on every entry" {
        $result = & $scriptPath -EventName push -PassThru
        foreach ($entry in @($result.Matrix.include)) {
            $expected = @($manifest.Variants | Where-Object { $_.Name -eq $entry.name })[0].Channel
            $entry.channel | Should -Be $expected
        }
    }

    It "derives the runner-executable comparison list from the full manifest even on pull requests" {
        $result = & $scriptPath -EventName pull_request -PassThru
        $expected = @($manifest.Variants |
            Where-Object { $_.Platform -eq 'x64' -and $_.RunnerCanExecute } |
            ForEach-Object { $_.Simd })
        @($result.RunnerExecutableVariants) | Should -Be $expected
    }
}
