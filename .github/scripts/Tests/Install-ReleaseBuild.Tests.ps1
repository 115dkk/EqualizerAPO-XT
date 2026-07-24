Describe "Install-ReleaseBuild.ps1" {
    BeforeAll {
        $scriptPath = Join-Path $PSScriptRoot "..\Install-ReleaseBuild.ps1"
        $channel = "x64-avx2"
        $asset = "EqualizerAPO-XT-$channel-$channel-Setup.exe"
    }

    BeforeEach {
        $download = Join-Path $TestDrive ([guid]::NewGuid().ToString())
        New-Item -ItemType Directory -Path $download | Out-Null
        $setup = Join-Path $download $asset
        [System.IO.File]::WriteAllBytes($setup, [byte[]](1, 2, 3, 4))
    }

    It "accepts a setup listed with its exact SHA-256" {
        $hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
        [System.IO.File]::WriteAllText((Join-Path $download "SHA256SUMS.txt"),
            "$hash  $asset`n")
        $result = & $scriptPath -Repository owner/repo -Tag v1 -Channel $channel `
            -DownloadDirectory $download -SkipDownload -SkipInstall `
            -SkipInstallRootResolution -PassThru
        $result.SetupPath | Should -Be $setup
    }

    It "rejects a checksum mismatch" {
        [System.IO.File]::WriteAllText((Join-Path $download "SHA256SUMS.txt"),
            "$('0' * 64)  $asset`n")
        {
            & $scriptPath -Repository owner/repo -Tag v1 -Channel $channel `
                -DownloadDirectory $download -SkipDownload -SkipInstall `
                -SkipInstallRootResolution
        } | Should -Throw "*Checksum mismatch*"
    }

    It "rejects a checksum file that omits the executable" {
        [System.IO.File]::WriteAllText((Join-Path $download "SHA256SUMS.txt"),
            "$('0' * 64)  another.exe`n")
        {
            & $scriptPath -Repository owner/repo -Tag v1 -Channel $channel `
                -DownloadDirectory $download -SkipDownload -SkipInstall `
                -SkipInstallRootResolution
        } | Should -Throw "*does not list*"
    }

    It "can tolerate a missing setup for cleanup-only reinstall steps" {
        Remove-Item -LiteralPath $setup
        {
            & $scriptPath -Repository owner/repo -Tag v1 -Channel $channel `
                -DownloadDirectory $download -SkipDownload -SkipInstallRootResolution `
                -AllowMissing
        } | Should -Not -Throw
    }
}
