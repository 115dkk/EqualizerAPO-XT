# Pester 5 tests for .github/scripts/Provisioning.psm1
#
# Provisioning.psm1 is the single implementation of the dependency
# download+verify loop, the vcpkg dependency build, and the Qt repository install
# that .github/workflows/build.yml and setup-build.ps1 used to duplicate
# (audit finding TD015). The download+verify loop is the supply-chain gate:
# a wrong URL fetches the wrong artifact, and a verification bug lets an
# unreviewed upload into shipped binaries. These tests lock down URL
# construction, pin resolution, and the hash-verification failure paths.
#
# No test touches the network: the module's only fetch primitive
# (Invoke-DependencyFetch) is mocked in module scope, and the extraction
# tests run against real zips created on the fly with Compress-Archive.
# Fixture manifests are written as actual .psd1 files and loaded with
# Import-PowerShellDataFile, the same restricted-language path production
# uses for .github/simd-variants.psd1.

BeforeDiscovery {
    # InModuleScope binds during discovery, before the shared BeforeAll runs.
    Import-Module (Resolve-Path (Join-Path $PSScriptRoot '..' 'Provisioning.psm1')).Path -Force
}

BeforeAll {
    $script:ModulePath = (Resolve-Path (Join-Path $PSScriptRoot '..' 'Provisioning.psm1')).Path
    Import-Module $script:ModulePath -Force

    $script:RepoManifestPath = (Resolve-Path (Join-Path $PSScriptRoot '..' '..' 'simd-variants.psd1')).Path
    $script:RepoManifest = Import-PowerShellDataFile -Path $script:RepoManifestPath

    $script:TempDirs = [System.Collections.Generic.List[string]]::new()

    function New-TempDir {
        $dir = Join-Path ([System.IO.Path]::GetTempPath()) ("provtest-" + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $dir | Out-Null
        $script:TempDirs.Add($dir)
        return $dir
    }

    # Creates a small real zip (Expand-Archive needs a valid archive) and
    # returns its path plus lowercase SHA-256, ready to pin in a fixture.
    function New-FixtureZip {
        param(
            [Parameter(Mandatory)] [string]$Directory,
            [Parameter(Mandatory)] [string]$Name,
            [string]$InnerFileName = 'payload.txt',
            [string]$Content = 'fixture payload'
        )
        $staging = New-TempDir
        Set-Content -Path (Join-Path $staging $InnerFileName) -Value $Content -Encoding ASCII
        $zipPath = Join-Path $Directory $Name
        Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zipPath -Force
        $sha = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
        return [pscustomobject]@{ Path = $zipPath; Sha256 = $sha; InnerFileName = $InnerFileName }
    }

    # Writes a minimal manifest fixture with the same shape as
    # .github/simd-variants.psd1 and loads it via Import-PowerShellDataFile.
    # Two variants: 'testy' (prebuilt assets) and 'vc' (UsesVcpkg).
    function New-FixtureManifest {
        param(
            [string]$MuparserxSha = 'aa11',
            [string]$FftwSha = 'bb22',
            [string]$SndfileSha = 'cc33',
            [string]$VelopackSha = 'dd44',
            [switch]$OmitFftwPin,
            [switch]$OmitMuparserxHash
        )

        $muparserxHashLine = if ($OmitMuparserxHash) { "'unrelated.zip' = 'ee55'" } else { "'mup.zip' = '$MuparserxSha'" }
        $fftwPinBlock = if ($OmitFftwPin) { '' } else {
@"
        '115dkk/amd-fftw' = @{
            Tag    = '5.9'
            Sha256 = @{ 'fftw.zip' = '$FftwSha' }
        }
"@
        }

        $text = @"
@{
    Variants = @(
        @{
            Name      = 'windows-x64-testy'
            Platform  = 'x64'
            Simd      = 'testy'
            Muparserx = 'mup.zip'
            Fftw      = 'fftw.zip'
            Sndfile   = 'snd.zip'
            UsesVcpkg = `$false
        }
        @{
            Name      = 'windows-x64-vc'
            Platform  = 'x64'
            Simd      = 'vc'
            Muparserx = 'mup.zip'
            Fftw      = `$null
            Sndfile   = `$null
            UsesVcpkg = `$true
        }
    )
    Shared = @{
        VelopackLibcVersion = '9.9.9'
        VelopackLibcSha256  = '$VelopackSha'
    }
    DependencyReleases = @{
        '115dkk/muparserx' = @{
            Tag    = '4.0.99'
            Sha256 = @{ $muparserxHashLine }
        }
$fftwPinBlock
        '115dkk/libsndfile' = @{
            Tag    = '1.9.9'
            Sha256 = @{ 'snd.zip' = '$SndfileSha' }
        }
    }
}
"@
        $path = Join-Path (New-TempDir) 'fixture-variants.psd1'
        Set-Content -Path $path -Value $text -Encoding ASCII
        return Import-PowerShellDataFile -Path $path
    }
}

AfterAll {
    foreach ($dir in $script:TempDirs) {
        Remove-Item -Path $dir -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Variable -Name ProvisioningTestGoodZip -Scope Global -ErrorAction SilentlyContinue
}

Describe "Provisioning.psm1" {
    Context "exported surface" {
        It "exports exactly the shared provisioning functions" {
            $exported = (Get-Command -Module Provisioning).Name | Sort-Object
            $exported | Should -Be @(
                'Build-VcpkgDependencies',
                'Get-DependencyDownloadSpec',
                'Get-PinnedSourceSpec',
                'Get-SdkDownloadSpec',
                'Get-SimdVariantEntry',
                'Install-PinnedSourceDependency',
                'Install-QtSdk',
                'Invoke-DependencyDownload',
                'Sync-PinnedCheckout'
            )
        }
    }

    Context "Get-PinnedSourceSpec against the repo manifest" {
        # Audit #348 TD-26: CI cloned these by tag alone. Each one now carries
        # the commit its tag must resolve to, straight from the manifest.
        It "pins every header-only source dependency to a full commit" {
            $deps = Join-Path (New-TempDir) 'deps'
            $specs = @(Get-PinnedSourceSpec -Manifest $script:RepoManifest -DepsRoot $deps)
            ($specs.Name | Sort-Object) | Should -Be @('Highway', 'TCLAP', 'VST3 pluginterfaces')
            foreach ($spec in $specs) {
                $spec.Tag | Should -Not -BeNullOrEmpty -Because "$($spec.Name) is cloned at a tag"
                $spec.Commit | Should -Match '^[0-9a-f]{40}$' -Because "$($spec.Name) is held to a full commit"
                $spec.RepoUrl | Should -Match '^https://github\.com/'
                $spec.CheckoutDir | Should -BeLike "$deps*"
            }
            ($specs | Where-Object Name -eq 'Highway').Commit | Should -Be $script:RepoManifest.Shared.HighwayCommit
            ($specs | Where-Object Name -eq 'VST3 pluginterfaces').CheckoutDir |
                Should -Be (Join-Path $deps 'vst3sdk\pluginterfaces')
        }
    }

    Context "Get-SimdVariantEntry" {
        It "finds a variant by platform and simd in the repo manifest" {
            $entry = Get-SimdVariantEntry -Manifest $script:RepoManifest -Platform 'x64' -Simd 'avx2'
            $entry.Name | Should -Be 'windows-x64-avx2'
        }

        It "throws the historical message for an unknown variant" {
            { Get-SimdVariantEntry -Manifest $script:RepoManifest -Platform 'x64' -Simd 'mmx' } |
                Should -Throw "No asset mapping for Platform=x64, Variant=mmx"
        }
    }

    Context "Get-SdkDownloadSpec against the repo manifest" {
        It "pins the ASIO SDK zip by URL and SHA-256 under deps/asiosdk" {
            $deps = Join-Path (New-TempDir) 'deps'
            $sdks = @(Get-SdkDownloadSpec -Manifest $script:RepoManifest -DepsRoot $deps)
            $sdks.Count | Should -Be 1
            $sdks[0].Repo | Should -Be 'steinberg/asiosdk'
            $sdks[0].Asset | Should -Be $script:RepoManifest.Shared.AsioSdkAsset
            $sdks[0].Url | Should -Be $script:RepoManifest.Shared.AsioSdkUrl
            $sdks[0].Url | Should -Match '^https://download\.steinberg\.net/sdk_downloads/.+\.zip$'
            $sdks[0].Sha256 | Should -Match '^[0-9a-fA-F]{64}$'
            $sdks[0].Destination | Should -Be (Join-Path $deps 'asiosdk')
        }

        It "refuses a manifest without the ASIO pin" {
            $manifest = @{ Shared = @{ AsioSdkAsset = 'x.zip'; AsioSdkUrl = 'https://example/x.zip' } }
            { Get-SdkDownloadSpec -Manifest $manifest -DepsRoot 'C:\deps' } | Should -Throw "*Shared.AsioSdkSha256*"
        }
    }

    Context "Get-DependencyDownloadSpec against the repo manifest" {
        BeforeAll {
            $script:DepsRoot = Join-Path (New-TempDir) 'deps'
        }

        It "builds the four avx2 downloads with pinned releases/download URLs" {
            $downloads = Get-DependencyDownloadSpec -Manifest $script:RepoManifest -DepsRoot $script:DepsRoot -Platform 'x64' -Simd 'avx2'
            $downloads.Count | Should -Be 4

            $mupTag = $script:RepoManifest.DependencyReleases['115dkk/muparserx'].Tag
            $fftwTag = $script:RepoManifest.DependencyReleases['115dkk/amd-fftw'].Tag
            $sndTag = $script:RepoManifest.DependencyReleases['115dkk/libsndfile'].Tag
            $veloVer = $script:RepoManifest.Shared.VelopackLibcVersion

            $byRepo = @{}
            foreach ($d in $downloads) { $byRepo[$d.Repo] = $d }

            $byRepo['115dkk/muparserx'].Url |
                Should -Be "https://github.com/115dkk/muparserx/releases/download/$mupTag/muparserx-msvc-release-x64-avx2.zip"
            $byRepo['115dkk/amd-fftw'].Url |
                Should -Be "https://github.com/115dkk/amd-fftw/releases/download/$fftwTag/fftw-windows-release-x64-avx2.zip"
            $byRepo['115dkk/libsndfile'].Url |
                Should -Be "https://github.com/115dkk/libsndfile/releases/download/$sndTag/libsndfile-x64-avx2.zip"
            $byRepo['velopack/velopack'].Url |
                Should -Be "https://github.com/velopack/velopack/releases/download/$veloVer/velopack_libc_$veloVer.zip"
        }

        It "targets the deps/ layout both consumers share" {
            $downloads = Get-DependencyDownloadSpec -Manifest $script:RepoManifest -DepsRoot $script:DepsRoot -Platform 'x64' -Simd 'avx2'
            $byRepo = @{}
            foreach ($d in $downloads) { $byRepo[$d.Repo] = $d }

            $byRepo['115dkk/muparserx'].Destination | Should -Be (Join-Path $script:DepsRoot 'muparserx')
            $byRepo['velopack/velopack'].Destination | Should -Be (Join-Path $script:DepsRoot 'velopack_libc')
            $byRepo['115dkk/amd-fftw'].Destination | Should -Be (Join-Path $script:DepsRoot 'fftw')
            $byRepo['115dkk/libsndfile'].Destination | Should -Be (Join-Path $script:DepsRoot 'libsndfile')
        }

        It "resolves the pinned SHA-256 for every avx2 asset" {
            $downloads = Get-DependencyDownloadSpec -Manifest $script:RepoManifest -DepsRoot $script:DepsRoot -Platform 'x64' -Simd 'avx2'
            foreach ($d in $downloads) {
                $d.Sha256 | Should -Match '^[0-9a-fA-F]{64}$' -Because "$($d.Asset) must carry a pinned hash"
            }
            ($downloads | Where-Object { $_.Repo -eq '115dkk/amd-fftw' }).Sha256 |
                Should -Be $script:RepoManifest.DependencyReleases['115dkk/amd-fftw'].Sha256['fftw-windows-release-x64-avx2.zip']
        }

        It "leaves FFTW and libsndfile to vcpkg for the sse2 variant" {
            $downloads = Get-DependencyDownloadSpec -Manifest $script:RepoManifest -DepsRoot $script:DepsRoot -Platform 'x64' -Simd 'sse2'
            @($downloads).Count | Should -Be 2
            @($downloads | ForEach-Object { $_.Repo }) | Sort-Object |
                Should -Be @('115dkk/muparserx', 'velopack/velopack')
        }

        It "selects the ARM64 assets for the neon variant" {
            $downloads = Get-DependencyDownloadSpec -Manifest $script:RepoManifest -DepsRoot $script:DepsRoot -Platform 'ARM64' -Simd 'neon'
            @($downloads | Where-Object { $_.Repo -eq '115dkk/amd-fftw' }).Asset | Should -Be 'fftw-windows-release-arm64.zip'
            @($downloads | Where-Object { $_.Repo -eq '115dkk/muparserx' }).Asset | Should -Be 'muparserx-msvc-release-ARM64.zip'
            @($downloads | Where-Object { $_.Repo -eq '115dkk/libsndfile' }).Asset | Should -Be 'libsndfile-arm64.zip'
        }

        It "resolves every variant in the repo manifest without error (drift guard)" {
            foreach ($variant in $script:RepoManifest.Variants) {
                $downloads = Get-DependencyDownloadSpec -Manifest $script:RepoManifest -DepsRoot $script:DepsRoot -Platform $variant.Platform -Simd $variant.Simd
                foreach ($d in $downloads) {
                    $d.Url | Should -Match '^https://github\.com/.+/releases/download/.+' -Because "$($variant.Name) / $($d.Asset) must download from a pinned tag, never releases/latest"
                    $d.Sha256 | Should -Not -BeNullOrEmpty -Because "$($variant.Name) / $($d.Asset) must be hash-pinned"
                }
            }
        }
    }

    Context "pin resolution from a fixture psd1" {
        BeforeAll {
            $script:FixtureDepsRoot = Join-Path (New-TempDir) 'deps'
        }

        It "builds URLs from the fixture's pinned tags" {
            $manifest = New-FixtureManifest
            $downloads = Get-DependencyDownloadSpec -Manifest $manifest -DepsRoot $script:FixtureDepsRoot -Platform 'x64' -Simd 'testy'
            @($downloads | Where-Object { $_.Repo -eq '115dkk/muparserx' }).Url |
                Should -Be 'https://github.com/115dkk/muparserx/releases/download/4.0.99/mup.zip'
            @($downloads | Where-Object { $_.Repo -eq 'velopack/velopack' }).Url |
                Should -Be 'https://github.com/velopack/velopack/releases/download/9.9.9/velopack_libc_9.9.9.zip'
        }

        It "throws when a referenced repo has no DependencyReleases pin" {
            $manifest = New-FixtureManifest -OmitFftwPin
            { Get-DependencyDownloadSpec -Manifest $manifest -DepsRoot $script:FixtureDepsRoot -Platform 'x64' -Simd 'testy' } |
                Should -Throw "No pinned tag or explicit URL for 115dkk/amd-fftw in simd-variants.psd1"
        }

        It "throws when the asset has no pinned SHA-256" {
            $manifest = New-FixtureManifest -OmitMuparserxHash
            { Get-DependencyDownloadSpec -Manifest $manifest -DepsRoot $script:FixtureDepsRoot -Platform 'x64' -Simd 'testy' } |
                Should -Throw "No pinned SHA-256 for mup.zip in simd-variants.psd1"
        }
    }

    Context "Invoke-DependencyDownload verify / cache-reuse logic" {
        BeforeEach {
            $script:DownloadRoot = Join-Path (New-TempDir) '_downloads'
            New-Item -ItemType Directory -Force -Path $script:DownloadRoot | Out-Null
            $script:Destination = Join-Path (New-TempDir) 'dest'
            $script:GoodZip = New-FixtureZip -Directory (New-TempDir) -Name 'dep.zip'
            # Mock bodies with -ModuleName run in the module's session state,
            # where the test file's $script: variables are invisible; hand the
            # fixture path over via a global instead.
            $global:ProvisioningTestGoodZip = $script:GoodZip.Path
        }

        It "reuses a cached zip without touching the network and still verifies + extracts it" {
            Mock -ModuleName Provisioning Invoke-DependencyFetch { throw "network must not be touched" }
            Copy-Item $script:GoodZip.Path (Join-Path $script:DownloadRoot 'dep.zip')

            $entry = @{
                Repo        = 'example/repo'
                Asset       = 'dep.zip'
                Url         = 'https://example.invalid/dep.zip'
                Sha256      = $script:GoodZip.Sha256
                Destination = $script:Destination
            }
            $output = Invoke-DependencyDownload -Downloads @($entry) -DownloadRoot $script:DownloadRoot -ReuseCachedDownloads 6>&1 | Out-String

            Should -Invoke Invoke-DependencyFetch -ModuleName Provisioning -Exactly -Times 0
            $output | Should -Match '\[cached\] dep\.zip'
            $output | Should -Match 'Verified SHA-256 for dep\.zip'
            Test-Path (Join-Path $script:Destination $script:GoodZip.InnerFileName) | Should -BeTrue
        }

        It "downloads when the zip is absent, then verifies and extracts" {
            Mock -ModuleName Provisioning Invoke-DependencyFetch {
                Copy-Item $global:ProvisioningTestGoodZip $OutFile
            }

            $entry = @{
                Repo        = 'example/repo'
                Asset       = 'dep.zip'
                Url         = 'https://example.invalid/dep.zip'
                Sha256      = $script:GoodZip.Sha256
                Destination = $script:Destination
            }
            Invoke-DependencyDownload -Downloads @($entry) -DownloadRoot $script:DownloadRoot -ReuseCachedDownloads 6>$null

            Should -Invoke Invoke-DependencyFetch -ModuleName Provisioning -Exactly -Times 1
            Test-Path (Join-Path $script:Destination $script:GoodZip.InnerFileName) | Should -BeTrue
        }

        It "removes a mismatching cached zip and points at the rerun (setup-build behavior)" {
            Mock -ModuleName Provisioning Invoke-DependencyFetch { throw "network must not be touched" }
            Copy-Item $script:GoodZip.Path (Join-Path $script:DownloadRoot 'dep.zip')

            $entry = @{
                Repo        = 'example/repo'
                Asset       = 'dep.zip'
                Url         = 'https://example.invalid/dep.zip'
                Sha256      = ('f' * 64)
                Destination = $script:Destination
            }
            { Invoke-DependencyDownload -Downloads @($entry) -DownloadRoot $script:DownloadRoot -ReuseCachedDownloads 6>$null } |
                Should -Throw "SHA-256 mismatch for dep.zip: expected *stale cache removed; rerun to redownload*"
            Test-Path (Join-Path $script:DownloadRoot 'dep.zip') | Should -BeFalse -Because "a poisoned cache entry must not survive for the next run"
            Test-Path $script:Destination | Should -BeFalse -Because "nothing may be extracted from an unverified zip"
        }

        It "fails a mismatching fresh download without the cache wording (CI behavior)" {
            Mock -ModuleName Provisioning Invoke-DependencyFetch {
                Copy-Item $global:ProvisioningTestGoodZip $OutFile
            }

            $entry = @{
                Repo        = 'example/repo'
                Asset       = 'dep.zip'
                Url         = 'https://example.invalid/dep.zip'
                Sha256      = ('f' * 64)
                Destination = $script:Destination
            }
            $failure = $null
            try {
                Invoke-DependencyDownload -Downloads @($entry) -DownloadRoot $script:DownloadRoot 6>$null
            } catch {
                $failure = $_
            }
            $failure | Should -Not -BeNullOrEmpty
            $failure.Exception.Message | Should -Match '^SHA-256 mismatch for dep\.zip: expected f{64}, got [0-9A-Fa-f]{64}$'
            Test-Path $script:Destination | Should -BeFalse -Because "nothing may be extracted from an unverified zip"
        }

        It "provisions a whole fixture-manifest variant from a pre-seeded cache without network (dry run)" {
            Mock -ModuleName Provisioning Invoke-DependencyFetch { throw "network must not be touched" }

            $zips = @{}
            foreach ($name in @('mup.zip', 'fftw.zip', 'snd.zip', 'velopack_libc_9.9.9.zip')) {
                $zips[$name] = New-FixtureZip -Directory $script:DownloadRoot -Name $name -InnerFileName "$name.txt"
            }
            $manifest = New-FixtureManifest -MuparserxSha $zips['mup.zip'].Sha256 `
                -FftwSha $zips['fftw.zip'].Sha256 `
                -SndfileSha $zips['snd.zip'].Sha256 `
                -VelopackSha $zips['velopack_libc_9.9.9.zip'].Sha256
            $depsRoot = Join-Path (New-TempDir) 'deps'

            $downloads = Get-DependencyDownloadSpec -Manifest $manifest -DepsRoot $depsRoot -Platform 'x64' -Simd 'testy'
            Invoke-DependencyDownload -Downloads $downloads -DownloadRoot $script:DownloadRoot -ReuseCachedDownloads 6>$null

            Should -Invoke Invoke-DependencyFetch -ModuleName Provisioning -Exactly -Times 0
            Test-Path (Join-Path $depsRoot 'muparserx\mup.zip.txt') | Should -BeTrue
            Test-Path (Join-Path $depsRoot 'fftw\fftw.zip.txt') | Should -BeTrue
            Test-Path (Join-Path $depsRoot 'libsndfile\snd.zip.txt') | Should -BeTrue
            Test-Path (Join-Path $depsRoot 'velopack_libc\velopack_libc_9.9.9.zip.txt') | Should -BeTrue
        }
    }

    InModuleScope Provisioning {
        Context "Qt repository install" {
            BeforeAll {
                $script:QtFixturePackage = 'qt.qt6.6120.win64_msvc2022_64'
                $script:QtFixtureStamp = '6.12.0-0-202609280346'
                $script:QtFixtureArchives = @{
                    qtbase         = 'qtbase-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
                    qtsvg          = 'qtsvg-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
                    qttools        = 'qttools-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
                    qttranslations = 'qttranslations-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
                }
                $script:QtFixtureXml = @"
<Updates>
  <PackageUpdate>
    <Name>$($script:QtFixturePackage)</Name>
    <Version>$($script:QtFixtureStamp)</Version>
    <DownloadableArchives>$($script:QtFixtureArchives.qtbase), $($script:QtFixtureArchives.qtsvg), qtdeclarative-Windows-X86_64.7z, qtdoc-Windows-X86_64.7z, $($script:QtFixtureArchives.qttools), $($script:QtFixtureArchives.qttranslations), d3dcompiler_47-x64.7z, opengl32sw-X86_64.7z</DownloadableArchives>
  </PackageUpdate>
  <PackageUpdate>
    <Name>qt.qt6.6120.win64_mingw</Name>
    <Version>6.12.0-0-irrelevant</Version>
    <DownloadableArchives>qtbase-Windows-MinGW.7z</DownloadableArchives>
  </PackageUpdate>
</Updates>
"@
            }

            BeforeEach {
                $script:QtTestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("qt-provtest-" + [guid]::NewGuid().ToString('N'))
                New-Item -ItemType Directory -Path $script:QtTestRoot | Out-Null
            }

            AfterEach {
                Remove-Item -Path $script:QtTestRoot -Recurse -Force -ErrorAction SilentlyContinue
            }

            It "constructs repository and package names for <Platform> Qt <QtVersion>" -TestCases @(
                @{
                    Platform = 'x64'; QtVersion = '6.12.0'; HostName = 'windows_x86'
                    Arch = 'win64_msvc2022_64'; ArchDir = 'msvc2022_64'; Compact = '6120'
                }
                @{
                    Platform = 'ARM64'; QtVersion = '6.12.0'; HostName = 'windows_arm64'
                    Arch = 'win64_msvc2022_arm64'; ArchDir = 'msvc2022_arm64'; Compact = '6120'
                }
                @{
                    Platform = 'x64'; QtVersion = '6.10.1'; HostName = 'windows_x86'
                    Arch = 'win64_msvc2022_64'; ArchDir = 'msvc2022_64'; Compact = '6101'
                }
                @{
                    Platform = 'ARM64'; QtVersion = '6.10.1'; HostName = 'windows_arm64'
                    Arch = 'win64_msvc2022_arm64'; ArchDir = 'msvc2022_arm64'; Compact = '6101'
                }
            ) {
                $spec = Get-QtRepositorySpec -QtVersion $QtVersion -Platform $Platform
                $spec.Host | Should -Be $HostName
                $spec.Arch | Should -Be $Arch
                $spec.ArchDir | Should -Be $ArchDir
                $spec.PackageName | Should -Be "qt.qt6.$Compact.$Arch"
                $root = "https://download.qt.io/online/qtsdkrepository/$HostName/desktop/qt6_$Compact"
                $spec.RepositoryUrls | Should -Be @(
                    "$root/qt6_$($Compact)_$ArchDir"
                    "$root/qt6_$Compact"
                ) -Because 'the per-architecture repository must be tried before the shared repository'
            }

            It "selects exactly the requested module archives and constructs their URLs" {
                $repository = 'https://download.qt.io/example/qt6_6120_msvc2022_64'
                $selected = @(Select-QtArchive -UpdatesXml $script:QtFixtureXml `
                        -PackageName $script:QtFixturePackage -RepositoryUrl $repository `
                        -Modules @('qtbase', 'qtsvg', 'qttools', 'qttranslations'))

                $selected.Count | Should -Be 4
                $selected.Module | Should -Be @('qtbase', 'qtsvg', 'qttools', 'qttranslations')
                foreach ($archive in $selected) {
                    $archive.Archive | Should -Be $script:QtFixtureArchives[$archive.Module]
                    $archive.Url | Should -Be "$repository/$($script:QtFixturePackage)/$($script:QtFixtureStamp)$($archive.Archive)"
                }
            }

            It "throws with the requested package name when Updates.xml lacks the package" {
                { Select-QtArchive -UpdatesXml $script:QtFixtureXml `
                        -PackageName 'qt.qt6.6120.missing_arch' -RepositoryUrl 'https://example.invalid/repo' `
                        -Modules @('qtbase') } |
                    Should -Throw '*qt.qt6.6120.missing_arch*was not found*'
            }

            It "throws when a requested module matches no archive" {
                { Select-QtArchive -UpdatesXml $script:QtFixtureXml `
                        -PackageName $script:QtFixturePackage -RepositoryUrl 'https://example.invalid/repo' `
                        -Modules @('qtnetworkauth') } |
                    Should -Throw '*Expected one qtnetworkauth archive*found 0*'
            }

            It "throws when a requested module matches more than one archive" {
                $duplicateXml = $script:QtFixtureXml.Replace(
                    "$($script:QtFixtureArchives.qtsvg),",
                    "$($script:QtFixtureArchives.qtsvg), qtsvg-second-Windows-X86_64.7z,")
                { Select-QtArchive -UpdatesXml $duplicateXml `
                        -PackageName $script:QtFixturePackage -RepositoryUrl 'https://example.invalid/repo' `
                        -Modules @('qtsvg') } |
                    Should -Throw '*Expected one qtsvg archive*found 2*'
            }

            It "falls back to the shared repository when the per-architecture fetch fails" {
                $perArch = 'https://example.invalid/per-arch'
                $shared = 'https://example.invalid/shared'
                Mock Invoke-DependencyFetch {
                    if ($Url.StartsWith($perArch, [System.StringComparison]::Ordinal)) {
                        throw 'fixture 404'
                    }
                    if ($Url.EndsWith('/Updates.xml', [System.StringComparison]::Ordinal)) {
                        [System.IO.File]::WriteAllText($OutFile, $script:QtFixtureXml, [System.Text.UTF8Encoding]::new($false))
                    }
                    else {
                        $xmlPath = $OutFile.Substring(0, $OutFile.Length - '.sha256'.Length)
                        $hash = (Get-FileHash -Path $xmlPath -Algorithm SHA256).Hash.ToLowerInvariant()
                        [System.IO.File]::WriteAllText($OutFile, "$hash Updates.xml", [System.Text.Encoding]::ASCII)
                    }
                }

                $metadata = Get-QtRepositoryMetadata -RepositoryUrls @($perArch, $shared) `
                    -MetadataRoot (Join-Path $script:QtTestRoot 'metadata')

                $metadata.RepositoryUrl | Should -Be $shared
                $metadata.UpdatesXml | Should -Be $script:QtFixtureXml
                Should -Invoke Invoke-DependencyFetch -Exactly -Times 1 -ParameterFilter { $Url -eq "$perArch/Updates.xml" }
                Should -Invoke Invoke-DependencyFetch -Exactly -Times 1 -ParameterFilter { $Url -eq "$shared/Updates.xml" }
                Should -Invoke Invoke-DependencyFetch -Exactly -Times 1 -ParameterFilter { $Url -eq "$shared/Updates.xml.sha256" }
            }

            It "throws when the Updates.xml SHA-256 sidecar does not match" {
                $repository = 'https://example.invalid/repository'
                Mock Invoke-DependencyFetch {
                    if ($Url.EndsWith('/Updates.xml', [System.StringComparison]::Ordinal)) {
                        [System.IO.File]::WriteAllText($OutFile, $script:QtFixtureXml, [System.Text.UTF8Encoding]::new($false))
                    }
                    else {
                        [System.IO.File]::WriteAllText($OutFile, (('0' * 64) + ' Updates.xml'), [System.Text.Encoding]::ASCII)
                    }
                }

                { Get-QtRepositoryMetadata -RepositoryUrls @($repository) `
                        -MetadataRoot (Join-Path $script:QtTestRoot 'metadata') } |
                    Should -Throw "SHA-256 mismatch for $repository/Updates.xml*"
            }

            It "removes an archive whose SHA-256 does not match its pin" {
                $downloadRoot = Join-Path $script:QtTestRoot 'archives'
                $archiveName = 'qtbase-fixture.7z'
                $manifest = @{
                    Shared = @{
                        QtVersion = '6.12.0'
                        QtArchiveSha256 = @{ x64 = @{ qtbase = ('f' * 64) } }
                    }
                }
                $archiveSpec = [pscustomobject]@{
                    Module = 'qtbase'; Archive = $archiveName; Url = 'https://example.invalid/qtbase-fixture.7z'
                }
                Mock Invoke-DependencyFetch {
                    [System.IO.File]::WriteAllBytes($OutFile, [byte[]]@(1, 2, 3, 4))
                }

                { Get-QtArchive -Archives @($archiveSpec) -DownloadRoot $downloadRoot `
                        -QtVersion '6.12.0' -Platform 'x64' -Manifest $manifest } |
                    Should -Throw "SHA-256 mismatch for ${archiveName}: expected $('f' * 64), got *"
                Test-Path (Join-Path $downloadRoot $archiveName) | Should -BeFalse
            }

            It "throws with the observed hash when a module has no SHA-256 pin" {
                $downloadRoot = Join-Path $script:QtTestRoot 'archives'
                $archiveName = 'qtbase-fixture.7z'
                $manifest = @{
                    Shared = @{
                        QtVersion = '6.12.0'
                        QtArchiveSha256 = @{ x64 = @{} }
                    }
                }
                $archiveSpec = [pscustomobject]@{
                    Module = 'qtbase'; Archive = $archiveName; Url = 'https://example.invalid/qtbase-fixture.7z'
                }
                Mock Invoke-DependencyFetch {
                    [System.IO.File]::WriteAllBytes($OutFile, [byte[]]@(5, 6, 7, 8))
                }

                { Get-QtArchive -Archives @($archiveSpec) -DownloadRoot $downloadRoot `
                        -QtVersion '6.12.0' -Platform 'x64' -Manifest $manifest } |
                    Should -Throw 'No SHA-256 pin for Qt 6.12.0 x64 qtbase in simd-variants.psd1 (observed *)'
            }
        }
    }

    Context "thin wrappers (parameter contracts only; no vcpkg/Qt runs here)" {
        It "Build-VcpkgDependencies only accepts the vcpkg-built variants sse2 and avx" {
            { Build-VcpkgDependencies -DepsRoot 'x' -VcpkgFallbackRoot 'y' -SimdVariant 'avx2' -VcpkgCommit 'z' } |
                Should -Throw -ErrorId 'ParameterArgumentValidationError,Build-VcpkgDependencies'
        }

        It "Install-QtSdk only accepts the platforms the manifest builds" {
            { Install-QtSdk -WorkspaceRoot 'x' -Platform 'x86' } |
                Should -Throw -ErrorId 'ParameterArgumentValidationError,Install-QtSdk'
        }
    }
}
