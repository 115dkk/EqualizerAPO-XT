#
# simd-variants.psd1 — single source of truth for the SIMD/architecture build matrix.
#
# WHY THIS FILE EXISTS
#   The per-variant facts (matrix name, MSVC instruction-set flag, dependency asset
#   zip names, Velopack/update channel) used to be re-hardcoded in eight .vcxproj,
#   three .pro files, .github/workflows/build.yml, setup-build.ps1 and
#   .github/scripts/New-ReleaseNotes.ps1. A typo in any copy silently mislabels a
#   build or breaks a download. This manifest defines them once; the PowerShell
#   consumers import it with Import-PowerShellDataFile (the restricted-language data
#   loader, so it is safe to import in CI).
#
# CONSUMERS
#   - .github/scripts/Provisioning.psm1     (shared download+verify / vcpkg /
#                                            Qt provisioning; expands Variants +
#                                            DependencyReleases into download specs)
#   - setup-build.ps1                       (dependency download + pinned tags/hashes,
#                                            via Provisioning.psm1)
#   - .github/workflows/build.yml           ("Resolve pinned dependency tags" step +
#                                            dependency download verification,
#                                            via Provisioning.psm1)
#   - .github/scripts/New-BuildMatrix.ps1   (expands Variants into the build.yml
#                                            job matrix — the matrix is no longer
#                                            hand-written in YAML)
#   - .github/scripts/Test-VariantSync.ps1  (fails CI when the channel strings
#                                            compiled into Installer/AutoInstaller.cpp
#                                            drift from Variants[].Channel)
#   - .github/scripts/New-ReleaseNotes.ps1  (release-channel guidance/sort table)
#   - .github/scripts/New-VelopackRelease.ps1 (packs each channel under its
#                                            Channel/Title identity)
#
#   NOT yet consumed: the .vcxproj files. Those keep their inline AVX2 default for
#   local builds (CI always overrides per variant) and report the chosen value at
#   build time.
#
# SCHEMA
#   Variants = ordered list of every variant CI builds. Each entry has:
#     Name        matrix.name in build.yml (e.g. "windows-x64-avx2").
#     Platform    "x64" or "ARM64" (matrix.platform).
#     Simd        matrix.simd_variant (e.g. "avx2", "neon").
#     ArchFlag    MSVC EnableEnhancedInstructionSet value for the .vcxproj /
#                 build.yml `arch_flag`. $null for variants that pass no override
#                 (sse2 expands to "NotSet"; ARM64 passes nothing).
#     QtArchFlag  /arch:* flag the .pro files compile with (matrix.qt_arch_flag).
#                 $null when none is passed (sse2 baseline, ARM64).
#     Channel     Velopack / EAPO_UPDATE_CHANNEL string (e.g. "x64-avx2", "arm64").
#     Title       Human-facing name Velopack stamps on the install: the Start
#                 menu/desktop shortcut, the Apps & Features entry and the
#                 per-channel Setup window. Kept distinct per variant so a user
#                 can tell which build is installed. Safe to rename between
#                 releases: velopack 1.1.1's update apply rewrites the
#                 uninstall entry and renames existing shortcuts (matched by
#                 target path, not by name), so installed apps migrate to a
#                 new Title on their next update without user action.
#     Fftw        amd-fftw release asset zip ($null when built from vcpkg).
#     Muparserx   muparserx release asset zip (always present).
#     Sndfile     libsndfile release asset zip ($null when built from vcpkg).
#     UsesVcpkg   $true when FFTW + libsndfile are built from vcpkg instead of being
#                 downloaded (x64 sse2 / avx only).
#     RunnerCanExecute  $true when GitHub-hosted runners can execute this variant's
#                 binaries at runtime. avx512/avx10_1 stay $false: CI builds and
#                 ships them but skips their runtime test steps. The cross-variant
#                 audio gating list (x64 variants with this flag) is derived from
#                 here by New-BuildMatrix.ps1 and handed to
#                 Tests/AudioRegressionTests/scripts/cross_variant_compare.py via
#                 the RUNNER_EXECUTABLE_VARIANTS env var in build.yml.
#     Primary     $true on exactly one variant. The primary variant is the only one
#                 pull requests build, and it seeds the audio regression reference
#                 set when Tests/AudioRegressionTests/references is empty.
#
#   Shared = pinned tags/versions that are NOT per-variant:
#     VelopackLibcVersion  velopack/velopack release tag for velopack_libc_<v>.zip.
#     VelopackLibcSha256   SHA-256 of that zip; bumped together with the version.
#     Vst3Tag              steinbergmedia/vst3_pluginterfaces git tag/ref.
#     HighwayTag           google/highway git tag/ref.
#     TclapTag             115dkk/tclap git tag/ref.
#     VcpkgCommit          microsoft/vcpkg commit for the sse2/avx vcpkg builds.
#     AsioSdkAsset         file name of the Steinberg ASIO SDK zip (GPLv3 dual-licensed
#                          since 2.3.4; fetched at build time, never vendored).
#     AsioSdkUrl           where that zip is downloaded from (no authentication).
#     AsioSdkSha256        SHA-256 of the zip; bumped together with the asset.
#
#   DependencyReleases = supply-chain pins for the prebuilt binary dependencies.
#     Keyed by GitHub repository. CI and setup-build.ps1 download from
#     releases/download/<Tag>/<asset> (never releases/latest) and refuse the file
#     when its SHA-256 does not match, so a new upload to these personal forks
#     cannot change shipped binaries without a reviewed diff in this manifest.
#     Hashes are the GitHub release asset digests (sha256:...) without the prefix.
#
# RULE: every string here must match what build.yml downloads and what the running
# binaries are labelled with. CI downloads assets by these exact names.
#
@{
    Variants = @(
        @{
            Name      = 'windows-x64-sse2'
            Platform  = 'x64'
            Simd      = 'sse2'
            ArchFlag  = $null                                   # expands to "NotSet"
            QtArchFlag = $null                                  # baseline code generation
            Channel   = 'x64-sse2'
            Title     = 'EQ APO XT'
            Fftw      = $null                                   # built from vcpkg
            # The avx2 zip is a SOURCE carrier here: CI recompiles muparserx from
            # parser/*.cpp with this variant's /arch flag (build.yml "Build
            # lower-SIMD dependency binaries"), so no AVX2 code is linked in.
            Muparserx = 'muparserx-msvc-release-x64-avx2.zip'
            Sndfile   = $null                                   # built from vcpkg
            UsesVcpkg = $true
            RunnerCanExecute = $true
        }
        @{
            Name      = 'windows-x64-avx'
            Platform  = 'x64'
            Simd      = 'avx'
            ArchFlag  = 'AdvancedVectorExtensions'
            QtArchFlag = '/arch:AVX'
            Channel   = 'x64-avx'
            Title     = 'EQ APO XT AVX'
            Fftw      = $null                                   # built from vcpkg
            # Source carrier, recompiled with /arch:AVX (see the sse2 note above).
            Muparserx = 'muparserx-msvc-release-x64-avx2.zip'
            Sndfile   = $null                                   # built from vcpkg
            UsesVcpkg = $true
            RunnerCanExecute = $true
        }
        @{
            Name      = 'windows-x64-avx2'
            Platform  = 'x64'
            Simd      = 'avx2'
            ArchFlag  = 'AdvancedVectorExtensions2'
            QtArchFlag = '/arch:AVX2'
            Channel   = 'x64-avx2'
            Title     = 'EQ APO XT AVX2'
            Fftw      = 'fftw-windows-release-x64-avx2.zip'
            Muparserx = 'muparserx-msvc-release-x64-avx2.zip'
            Sndfile   = 'libsndfile-x64-avx2.zip'
            UsesVcpkg = $false
            RunnerCanExecute = $true
            Primary   = $true
        }
        @{
            Name      = 'windows-x64-avx512'
            Platform  = 'x64'
            Simd      = 'avx512'
            ArchFlag  = 'AdvancedVectorExtensions512'
            QtArchFlag = '/arch:AVX512'
            Channel   = 'x64-avx512'
            Title     = 'EQ APO XT AVX-512'
            Fftw      = 'fftw-windows-release-x64-avx512.zip'
            Muparserx = 'muparserx-msvc-release-x64-avx512.zip'
            Sndfile   = 'libsndfile-x64-avx512.zip'
            UsesVcpkg = $false
            RunnerCanExecute = $false                           # hosted x64 runners do not guarantee AVX-512
        }
        @{
            Name      = 'windows-x64-avx10_1'
            Platform  = 'x64'
            Simd      = 'avx10_1'
            ArchFlag  = 'AdvancedVectorExtensions101'
            QtArchFlag = '/arch:AVX10.1'
            Channel   = 'x64-avx10-1'
            Title     = 'EQ APO XT AVX10'
            Fftw      = 'fftw-windows-release-x64-avx10.zip'
            Muparserx = 'muparserx-msvc-release-x64-avx10.zip'
            Sndfile   = 'libsndfile-x64-avx10.zip'
            UsesVcpkg = $false
            RunnerCanExecute = $false                           # hosted x64 runners do not guarantee AVX10.1
        }
        @{
            Name      = 'windows-arm64'
            Platform  = 'ARM64'
            Simd      = 'neon'
            ArchFlag  = $null                                   # ARM64 passes no /arch override
            QtArchFlag = $null
            Channel   = 'arm64-neon'                            # matches the published Velopack channel
            Title     = 'EQ APO XT Neon'
            Fftw      = 'fftw-windows-release-arm64.zip'
            Muparserx = 'muparserx-msvc-release-ARM64.zip'
            Sndfile   = 'libsndfile-arm64.zip'
            UsesVcpkg = $false
            RunnerCanExecute = $true                            # windows-11-arm runs NEON natively
        }
    )

    Shared = @{
        # Qt plugin folders windeployqt deploys next to the Qt apps. Spelled
        # once here: Package-Artifacts.ps1 asserts the staged artifact carries
        # no DLL folder outside this list (so a new windeployqt folder fails
        # the build instead of silently missing from releases), and
        # New-VelopackRelease.ps1 relocates exactly these folders under qt\
        # (Editor.exe calls addLibraryPath("qt")).
        QtPluginFolders     = @('generic', 'iconengines', 'imageformats',
                                'networkinformation', 'platforms', 'styles', 'tls')
        # velopack_libc ships as a single cross-platform zip attached to the
        # velopack/velopack release. The asset name is velopack_libc_<version>.zip.
        VelopackLibcVersion = '1.1.1'
        # dotnet tool package used to build release assets. Bump together with
        # VelopackLibcVersion after reviewing packer changes.
        VelopackVpkVersion  = '1.1.1'
        # SHA-256 of velopack_libc_<VelopackLibcVersion>.zip, verified like the
        # DependencyReleases assets below. Bump together with VelopackLibcVersion.
        VelopackLibcSha256  = '7b77d378226e4c5b110565dbe1c718cc91eadbf0c4be8b8e6af9ed8ea6202cb1'
        # steinbergmedia/vst3_pluginterfaces — pinned to the first MIT-licensed tag.
        # Audit #250 F058: git tags are movable, so each source pin also
        # records the commit the tag resolved to when it was reviewed;
        # setup-build.ps1 asserts the checkout matches.
        Vst3Tag             = 'v3.8.0_build_66'
        Vst3Commit          = '31d6eeba6daaa3e2a8bfbe3e7a90ca0b7fbfbc1c'
        # google/highway — header-only portable SIMD.
        HighwayTag          = '1.4.0'
        HighwayCommit       = '2607d3b5b0113992fe84d3848859eae13b3b52c1'
        # 115dkk/tclap — header-only CLI parser (tag 1.2.5 = fork HEAD).
        TclapTag            = '1.2.5'
        TclapCommit         = '77561f5fab620e0857a04c240ae981f679449e15'
        # Audit #250 F059: the Qt toolchain version used to default
        # independently in setup-build.ps1 (local) and Provisioning.psm1 (CI);
        # bumping one silently split local from CI. Both read this field now.
        QtVersion           = '6.10.1'
        # microsoft/vcpkg — commit the sse2/avx dependency builds (FFTW,
        # libsndfile) check out. Pinned so a moving vcpkg HEAD cannot silently
        # change the portfiles those builds compile from; bump deliberately.
        VcpkgCommit         = 'd87340acc46bdeda386037b38aca30136e667e47'
        # Steinberg ASIO SDK 2.3.4 (2025-10-15), the first dual-licensed
        # (proprietary or GPLv3) release. www.steinberg.net/asiosdk redirects
        # here without authentication. Consumed by the ASIO wrapper, the fake
        # driver and the probe (docs/architecture/asio-host-study.md).
        AsioSdkAsset        = 'ASIO-SDK_2.3.4_2025-10-15.zip'
        AsioSdkUrl          = 'https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip'
        AsioSdkSha256       = 'd5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca'
    }

    # The prebuilt binaries are served from 115dkk-owned forks (audit #146
    # TD036): the originals lived on a third-party account, so availability
    # depended on it. The forks carry byte-identical assets (hashes below are
    # unchanged) mirrored from TheFireKahuna releases of the same tags.
    DependencyReleases = @{
        '115dkk/amd-fftw' = @{
            Tag    = '5.1'
            Sha256 = @{
                'fftw-windows-release-x64-avx2.zip'   = '5b2a56aededb8503b064e4e70dfdf9cf5c23f7b5220eaa47abfd6d3362344d0e'
                'fftw-windows-release-x64-avx512.zip' = 'a478b17be08055ffe4dd8736a0c0652c2a65c721269c30feeef0efd00a9bda35'
                'fftw-windows-release-x64-avx10.zip'  = 'a7aa5edeb06f5cf22440266d47c8760dbbe878a429962b6f6d605fe40225b951'
                'fftw-windows-release-arm64.zip'      = '26ef7e10fe47426e601f45e9767f468f407d85c4f4011985185f7d8926464529'
            }
        }
        '115dkk/muparserx' = @{
            Tag    = '4.0.13'
            Sha256 = @{
                'muparserx-msvc-release-x64-avx2.zip'   = '5639167ce626c85a28f5c71f5f716533097d09895d388717177be9191d1f4b0e'
                'muparserx-msvc-release-x64-avx512.zip' = '2b987bb3662d69152dd13b0df0b4a16906649d26e6c098450768fbc633b2b5a5'
                'muparserx-msvc-release-x64-avx10.zip'  = '11aeea3c276a61b77355ba9f746e0b5b58a51f0c8e7edcfea4c71ef1ba3863be'
                'muparserx-msvc-release-ARM64.zip'      = 'e4e3391e557a4bd61686efed886f49c469c9e274a3a1a3ba2cbeaf58b51d0c42'
            }
        }
        '115dkk/libsndfile' = @{
            Tag    = '1.2.2'
            Sha256 = @{
                'libsndfile-x64-avx2.zip'   = '7613683be2e9a826c36d9aa5ec5afdad1907b6e80e877e36501e6610fefa7df0'
                'libsndfile-x64-avx512.zip' = 'f7ea617640ef68234cd6c2cc214a3ec846af935d2fdf46196c0821925fa0bb59'
                'libsndfile-x64-avx10.zip'  = 'd48cf3036e7a7aa30d129b25293bb29d175adfff1fa095a220d702b4bf40a919'
                'libsndfile-arm64.zip'      = '3c9218c40e97c60f95eb4ac075f57dee20090d135073025e34210d9cacdb2181'
            }
        }
    }
}
