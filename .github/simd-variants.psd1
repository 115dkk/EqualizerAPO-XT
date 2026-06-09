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
#   - setup-build.ps1                       (dependency download + pinned tags)
#   - .github/workflows/build.yml           ("Resolve pinned dependency tags" step)
#   - .github/scripts/New-ReleaseNotes.ps1  (release-channel guidance/sort table)
#
#   NOT yet consumed: the .vcxproj files and the build.yml `matrix.include` block.
#   Those keep their inline values for now (see notes below) and remain a follow-up
#   consolidation target. Their values MUST stay equal to this manifest.
#
# SCHEMA
#   Variants = ordered list of every variant CI builds. Each entry has:
#     Name        matrix.name in build.yml (e.g. "windows-x64-avx2").
#     Platform    "x64" or "ARM64" (matrix.platform).
#     Simd        matrix.simd_variant (e.g. "avx2", "neon").
#     ArchFlag    MSVC EnableEnhancedInstructionSet value for the .vcxproj /
#                 build.yml `arch_flag`. $null for variants that pass no override
#                 (sse2 uses "NotSet"; ARM64 passes nothing).
#     QtArchFlag  /arch:* flag the .pro files compile with (matrix.qt_arch_flag).
#                 $null when none is passed (sse2 baseline, ARM64).
#     Channel     Velopack / EAPO_UPDATE_CHANNEL string (e.g. "x64-avx2", "arm64").
#     Fftw        amd-fftw release asset zip ($null when built from vcpkg).
#     Muparserx   muparserx release asset zip (always present).
#     Sndfile     libsndfile release asset zip ($null when built from vcpkg).
#     UsesVcpkg   $true when FFTW + libsndfile are built from vcpkg instead of being
#                 downloaded (x64 sse2 / avx only).
#
#   Shared = pinned tags/versions that are NOT per-variant:
#     VelopackLibcVersion  velopack/velopack release tag for velopack_libc_<v>.zip.
#     Vst3Tag              steinbergmedia/vst3_pluginterfaces git tag/ref.
#     HighwayTag           google/highway git tag/ref.
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
            ArchFlag  = $null                                   # matrix arch_flag is "NotSet"
            QtArchFlag = $null                                  # baseline code generation
            Channel   = 'x64-sse2'
            Fftw      = $null                                   # built from vcpkg
            Muparserx = 'muparserx-msvc-release-x64-avx2.zip'
            Sndfile   = $null                                   # built from vcpkg
            UsesVcpkg = $true
        }
        @{
            Name      = 'windows-x64-avx'
            Platform  = 'x64'
            Simd      = 'avx'
            ArchFlag  = 'AdvancedVectorExtensions'
            QtArchFlag = '/arch:AVX'
            Channel   = 'x64-avx'
            Fftw      = $null                                   # built from vcpkg
            Muparserx = 'muparserx-msvc-release-x64-avx2.zip'
            Sndfile   = $null                                   # built from vcpkg
            UsesVcpkg = $true
        }
        @{
            Name      = 'windows-x64-avx2'
            Platform  = 'x64'
            Simd      = 'avx2'
            ArchFlag  = 'AdvancedVectorExtensions2'
            QtArchFlag = '/arch:AVX2'
            Channel   = 'x64-avx2'
            Fftw      = 'fftw-windows-release-x64-avx2.zip'
            Muparserx = 'muparserx-msvc-release-x64-avx2.zip'
            Sndfile   = 'libsndfile-x64-avx2.zip'
            UsesVcpkg = $false
        }
        @{
            Name      = 'windows-x64-avx512'
            Platform  = 'x64'
            Simd      = 'avx512'
            ArchFlag  = 'AdvancedVectorExtensions512'
            QtArchFlag = '/arch:AVX512'
            Channel   = 'x64-avx512'
            Fftw      = 'fftw-windows-release-x64-avx512.zip'
            Muparserx = 'muparserx-msvc-release-x64-avx512.zip'
            Sndfile   = 'libsndfile-x64-avx512.zip'
            UsesVcpkg = $false
        }
        @{
            Name      = 'windows-x64-avx10_1'
            Platform  = 'x64'
            Simd      = 'avx10_1'
            ArchFlag  = 'AdvancedVectorExtensions101'
            QtArchFlag = '/arch:AVX10.1'
            Channel   = 'x64-avx10-1'
            Fftw      = 'fftw-windows-release-x64-avx10.zip'
            Muparserx = 'muparserx-msvc-release-x64-avx10.zip'
            Sndfile   = 'libsndfile-x64-avx10.zip'
            UsesVcpkg = $false
        }
        @{
            Name      = 'windows-arm64'
            Platform  = 'ARM64'
            Simd      = 'neon'
            ArchFlag  = $null                                   # ARM64 passes no /arch override
            QtArchFlag = $null
            Channel   = 'arm64-neon'                            # matches the published Velopack channel
            Fftw      = 'fftw-windows-release-arm64.zip'
            Muparserx = 'muparserx-msvc-release-ARM64.zip'
            Sndfile   = 'libsndfile-arm64.zip'
            UsesVcpkg = $false
        }
    )

    Shared = @{
        # velopack_libc ships as a single cross-platform zip attached to the
        # velopack/velopack release. The asset name is velopack_libc_<version>.zip.
        VelopackLibcVersion = '1.1.1'
        # steinbergmedia/vst3_pluginterfaces — pinned to the first MIT-licensed tag.
        Vst3Tag             = 'v3.8.0_build_66'
        # google/highway — header-only portable SIMD.
        HighwayTag          = '1.4.0'
    }
}
