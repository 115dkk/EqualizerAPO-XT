<#
.SYNOPSIS
    Shared build-dependency provisioning for CI and local setup: pinned
    release-asset download + SHA-256 verification, the vcpkg-based FFTW /
    libsndfile / muparserx rebuild for the sse2/avx variants, and the
    aqt-based Qt install.

.DESCRIPTION
    .github/workflows/build.yml and setup-build.ps1 used to each carry their
    own copy of these three provisioning steps, and the copies drifted (audit
    finding TD015). This module is the single implementation; both consumers
    import it with a relative path:

      Import-Module .\.github\scripts\Provisioning.psm1 -Force

    Exported functions:
      Get-SimdVariantEntry        variant lookup in the simd-variants.psd1 manifest
      Get-DependencyDownloadSpec  manifest -> download list (URLs + SHA-256 pins)
      Invoke-DependencyDownload   download / cache-reuse, verify, extract loop
      Build-VcpkgDependencies     vcpkg FFTW+libsndfile build & muparserx rebuild
      Install-QtSdk               aqt-based Qt install, returns the Qt root

    All pinned names, tags, and hashes come from .github/simd-variants.psd1
    (Import-PowerShellDataFile at the call site); this module never hardcodes
    an asset name or hash.
#>

# Module functions do not inherit the caller's preference variables, so set
# fail-fast behaviour here to match the inline `$ErrorActionPreference = "Stop"`
# both consumers used before the extraction.
$ErrorActionPreference = 'Stop'

function Get-SimdVariantEntry {
    <#
    .SYNOPSIS
        Returns the manifest Variants entry for a Platform/Simd pair, or throws.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [hashtable]$Manifest,
        [Parameter(Mandatory)] [ValidateSet('x64', 'ARM64')] [string]$Platform,
        [Parameter(Mandatory)] [string]$Simd
    )

    $entry = $Manifest.Variants |
        Where-Object { $_.Platform -eq $Platform -and $_.Simd -eq $Simd } |
        Select-Object -First 1
    if (-not $entry) {
        throw "No asset mapping for Platform=$Platform, Variant=$Simd"
    }
    return $entry
}

function Get-DependencyDownloadSpec {
    <#
    .SYNOPSIS
        Expands the manifest into the list of prebuilt binary downloads for one
        variant: muparserx and velopack_libc always, plus FFTW and libsndfile
        for the variants that do not build them from vcpkg.

    .DESCRIPTION
        Each returned hashtable carries Repo, Asset, Url, Sha256, and
        Destination (under $DepsRoot). URLs always use releases/download/<Tag>
        (never releases/latest) and every entry carries the SHA-256 recorded in
        simd-variants.psd1, so a new upload to the personal dependency forks
        cannot change what gets linked without a reviewed manifest diff.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [hashtable]$Manifest,
        [Parameter(Mandatory)] [string]$DepsRoot,
        [Parameter(Mandatory)] [ValidateSet('x64', 'ARM64')] [string]$Platform,
        [Parameter(Mandatory)] [string]$Simd
    )

    $variant = Get-SimdVariantEntry -Manifest $Manifest -Platform $Platform -Simd $Simd
    $pins = $Manifest.DependencyReleases

    # Resolves one DependencyReleases-pinned asset to a download entry.
    function Resolve-PinnedDownload {
        param([string]$Repo, [string]$Asset, [string]$DestinationLeaf)

        $pin = $pins[$Repo]
        if (-not $pin) {
            throw "No pinned tag or explicit URL for $Repo in simd-variants.psd1"
        }
        $sha256 = $pin.Sha256[$Asset]
        if (-not $sha256) {
            throw "No pinned SHA-256 for $Asset in simd-variants.psd1"
        }
        return @{
            Repo        = $Repo
            Asset       = $Asset
            Url         = "https://github.com/$Repo/releases/download/$($pin.Tag)/$Asset"
            Sha256      = $sha256
            Destination = Join-Path $DepsRoot $DestinationLeaf
        }
    }

    $velopackVersion = $Manifest.Shared.VelopackLibcVersion
    $downloads = @(
        (Resolve-PinnedDownload -Repo '115dkk/muparserx' -Asset $variant.Muparserx -DestinationLeaf 'muparserx'),
        @{
            # Velopack C/C++ runtime (Velopack.h + import libs + DLLs for every
            # platform). Always fetched regardless of SIMD variant; the Editor
            # links it for auto-update. Not covered by DependencyReleases (the
            # URL is release-tag based already), so its SHA-256 pin rides on
            # Shared.VelopackLibcVersion / Shared.VelopackLibcSha256.
            Repo        = 'velopack/velopack'
            Asset       = "velopack_libc_$velopackVersion.zip"
            Url         = "https://github.com/velopack/velopack/releases/download/$velopackVersion/velopack_libc_$velopackVersion.zip"
            Sha256      = $Manifest.Shared.VelopackLibcSha256
            Destination = Join-Path $DepsRoot 'velopack_libc'
        }
    )

    if (-not $variant.UsesVcpkg) {
        $downloads += (Resolve-PinnedDownload -Repo '115dkk/amd-fftw' -Asset $variant.Fftw -DestinationLeaf 'fftw')
        $downloads += (Resolve-PinnedDownload -Repo '115dkk/libsndfile' -Asset $variant.Sndfile -DestinationLeaf 'libsndfile')
    }

    return $downloads
}

function Get-SdkDownloadSpec {
    <#
    .SYNOPSIS
        The source SDK zips that are downloaded (not cloned) and are the same
        for every variant: today the Steinberg ASIO SDK.

    .DESCRIPTION
        Kept apart from Get-DependencyDownloadSpec because that list is
        per-variant and its drift guard requires GitHub release URLs; the ASIO
        SDK comes from Steinberg's own download host. The entries have the
        same shape, so Invoke-DependencyDownload takes both lists. The zip
        carries a top-level ASIOSDK\ folder, which is why the ASIO_SDK
        property points one level below the destination.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [hashtable]$Manifest,
        [Parameter(Mandatory)] [string]$DepsRoot
    )

    $shared = $Manifest.Shared
    foreach ($field in 'AsioSdkAsset', 'AsioSdkUrl', 'AsioSdkSha256') {
        if (-not $shared[$field]) { throw "simd-variants.psd1 Shared.$field is missing" }
    }
    return @(
        @{
            Repo        = 'steinberg/asiosdk'
            Asset       = $shared.AsioSdkAsset
            Url         = $shared.AsioSdkUrl
            Sha256      = $shared.AsioSdkSha256
            Destination = Join-Path $DepsRoot 'asiosdk'
        }
    )
}

function Invoke-DependencyFetch {
    # Internal: single network fetch, kept separate so tests can mock the
    # network away. Same curl invocation both consumers used inline.
    param(
        [Parameter(Mandatory)] [string]$Url,
        [Parameter(Mandatory)] [string]$OutFile
    )

    curl.exe --fail --location --retry 5 --retry-delay 5 --output $OutFile $Url
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download $Url"
    }
}

function Invoke-DependencyDownload {
    <#
    .SYNOPSIS
        Downloads (or reuses cached) release asset zips, verifies their SHA-256
        against the pinned value, and extracts them to their destinations.

    .DESCRIPTION
        Entries come from Get-DependencyDownloadSpec. A download whose SHA-256
        does not match the pin is refused. With -ReuseCachedDownloads (local
        setup-build.ps1 use) an already-present zip in $DownloadRoot is not
        re-downloaded, and a mismatching zip is deleted so the next run
        redownloads it; without the switch (CI use, fresh runner) every asset
        is downloaded and a mismatch fails hard without touching the file.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [hashtable[]]$Downloads,
        [Parameter(Mandatory)] [string]$DownloadRoot,
        [switch]$ReuseCachedDownloads
    )

    New-Item -ItemType Directory -Force -Path $DownloadRoot | Out-Null

    foreach ($download in $Downloads) {
        $zipPath = Join-Path $DownloadRoot $download.Asset

        if ($ReuseCachedDownloads -and (Test-Path $zipPath)) {
            Write-Host "  [cached] $($download.Asset)"
        }
        else {
            Write-Host "Downloading $($download.Repo) release asset $($download.Asset)"
            Invoke-DependencyFetch -Url $download.Url -OutFile $zipPath
        }

        if ($download.Sha256) {
            $actual = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash
            if ($actual -ne $download.Sha256) {
                if ($ReuseCachedDownloads) {
                    Remove-Item $zipPath -Force
                    throw "SHA-256 mismatch for $($download.Asset): expected $($download.Sha256), got $actual (stale cache removed; rerun to redownload)"
                }
                throw "SHA-256 mismatch for $($download.Asset): expected $($download.Sha256), got $actual"
            }
            Write-Host "Verified SHA-256 for $($download.Asset)"
        }

        New-Item -ItemType Directory -Force -Path $download.Destination | Out-Null
        Expand-Archive -Path $zipPath -DestinationPath $download.Destination -Force
        Write-Host "Extracted $($download.Asset) -> $($download.Destination)"
    }
}

function Copy-RequiredFile {
    # Internal: copies the first file under $SourceRoot matching any of
    # $Patterns (after $Filter) to $Destination, or throws. Used by the vcpkg
    # dependency build to pick the right FFTW/libsndfile artifacts.
    param(
        [string]$SourceRoot,
        [string[]]$Patterns,
        [scriptblock]$Filter,
        [string]$Destination
    )

    foreach ($pattern in $Patterns) {
        $found = @(Get-ChildItem -Path $SourceRoot -Recurse -Filter $pattern -File -ErrorAction SilentlyContinue | Where-Object $Filter)
        if ($found.Count -gt 0) {
            New-Item -ItemType Directory -Force -Path (Split-Path $Destination -Parent) | Out-Null
            Copy-Item -Path $found[0].FullName -Destination $Destination -Force
            Write-Host "Copied $($found[0].FullName) -> $Destination"
            return
        }
    }

    throw "Could not find required file under $SourceRoot matching $($Patterns -join ', ')"
}

function Build-VcpkgDependencies {
    <#
    .SYNOPSIS
        Builds FFTW and libsndfile from vcpkg and rebuilds muparserx from
        source with the variant's /arch flag, for the x64 sse2/avx variants
        that have no prebuilt binary assets.

    .DESCRIPTION
        Requires an imported MSVC environment (cl.exe / lib.exe on PATH):
        dot-source Import-VsDevEnvironment.ps1 and call
        Import-VsDevEnvironment "x64" before calling this. Prefers a
        preinstalled vcpkg ($env:VCPKG_INSTALLATION_ROOT, as on GitHub
        runners); otherwise clones vcpkg into $VcpkgFallbackRoot pinned to
        $VcpkgCommit. Output lands in the same deps/ layout the download path
        produces (fftw/include, fftw/Release, libsndfile/include,
        libsndfile/build/Release, muparserx/build/Release).
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$DepsRoot,
        [Parameter(Mandatory)] [string]$VcpkgFallbackRoot,
        [Parameter(Mandatory)] [ValidateSet('sse2', 'avx')] [string]$SimdVariant,
        [Parameter(Mandatory)] [string]$VcpkgCommit
    )

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "cl.exe is not on PATH; import the MSVC environment first (Import-VsDevEnvironment)"
    }

    $vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
    if (-not $vcpkgRoot -or -not (Test-Path (Join-Path $vcpkgRoot ".git"))) {
        $vcpkgRoot = $VcpkgFallbackRoot
        if (-not (Test-Path (Join-Path $vcpkgRoot ".git"))) {
            # Pin vcpkg to the manifest commit: cloning a moving HEAD would let
            # the portfiles (and thus the FFTW/libsndfile binaries built here)
            # change without a reviewed diff in simd-variants.psd1. --depth 1
            # keeps the clone small; the pinned commit is fetched by SHA and
            # checked out.
            git clone --depth 1 https://github.com/microsoft/vcpkg $vcpkgRoot
            if ($LASTEXITCODE -ne 0) { throw "Failed to clone vcpkg" }
        }
    }

    # GitHub runner images also provide a git checkout. Pin that checkout just
    # like the fallback clone so CI and local builds use identical portfiles.
    git -C $vcpkgRoot fetch --depth 1 origin $VcpkgCommit
    if ($LASTEXITCODE -ne 0) { throw "Failed to fetch pinned vcpkg commit $VcpkgCommit" }
    git -C $vcpkgRoot checkout --detach $VcpkgCommit
    if ($LASTEXITCODE -ne 0) { throw "Failed to check out pinned vcpkg commit $VcpkgCommit" }
    $effectiveVcpkgCommit = (git -C $vcpkgRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $effectiveVcpkgCommit -ne $VcpkgCommit) {
        throw "Effective vcpkg commit '$effectiveVcpkgCommit' does not match pin '$VcpkgCommit'"
    }
    Write-Host "Using pinned vcpkg commit $effectiveVcpkgCommit"
    # The runner image ships a prebuilt vcpkg.exe for *its* checkout. After
    # the tree is pinned to the manifest commit, that binary and the pinned
    # scripts can disagree ("document schema version 1 is not supported by
    # this version of vcpkg" took down the sse2/avx release builds when the
    # image updated). Always bootstrap from the pinned tree so the binary
    # matches the scripts it runs against.
    $vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
    if (Test-Path $vcpkgExe) {
        Remove-Item $vcpkgExe -Force
    }
    & (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed" }
    $fftwFeature = if ($SimdVariant -eq "avx") { "fftw3[avx,threads]:x64-windows" } else { "fftw3[sse2,threads]:x64-windows" }
    & $vcpkgExe install $fftwFeature "libsndfile:x64-windows" --clean-after-build
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg dependency install failed"
    }

    $installedRoot = Join-Path $vcpkgRoot "installed\x64-windows"
    $fftwInclude = Join-Path $DepsRoot "fftw\include"
    $fftwRelease = Join-Path $DepsRoot "fftw\Release"
    $sndInclude = Join-Path $DepsRoot "libsndfile\include"
    $sndRelease = Join-Path $DepsRoot "libsndfile\build\Release"

    New-Item -ItemType Directory -Force -Path $fftwInclude, $fftwRelease, $sndInclude, $sndRelease | Out-Null
    Copy-Item -Path (Join-Path $installedRoot "include\fftw3.h") -Destination $fftwInclude -Force
    Copy-Item -Path (Join-Path $installedRoot "include\sndfile*.h*") -Destination $sndInclude -Force

    # The project links the double-precision FFTW only; filter out the float /
    # long-double / threading variants vcpkg may also produce.
    $doubleFftwFilter = { $_.Name -notmatch "fftw3f|fftw3l|threads|omp" }
    Copy-RequiredFile (Join-Path $installedRoot "lib") @("libfftw3.lib", "fftw3.lib", "libfftw3-3.lib") $doubleFftwFilter (Join-Path $fftwRelease "libfftw3.lib")
    Copy-RequiredFile (Join-Path $installedRoot "lib") @("sndfile.lib") { $true } (Join-Path $sndRelease "sndfile.lib")

    Get-ChildItem -Path (Join-Path $installedRoot "bin") -Filter "*.dll" -File | Copy-Item -Destination $sndRelease -Force
    $fftwDlls = @(Get-ChildItem -Path (Join-Path $installedRoot "bin") -Filter "*fftw3*.dll" -File | Where-Object $doubleFftwFilter)
    if ($fftwDlls.Count -eq 0) {
        throw "Could not find FFTW runtime DLLs in vcpkg output"
    }
    $fftwDlls | Copy-Item -Destination $fftwRelease -Force
    if (-not (Test-Path (Join-Path $fftwRelease "libfftw3.dll"))) {
        Copy-Item -Path $fftwDlls[0].FullName -Destination (Join-Path $fftwRelease "libfftw3.dll") -Force
    }

    # Rebuild muparserx from the downloaded source carrier with this variant's
    # /arch flag (the prebuilt avx2 zip only supplies parser/*.cpp here), so no
    # AVX2 code is linked into the sse2/avx builds.
    $parserDir = Join-Path $DepsRoot "muparserx\parser"
    $muparserBuildDir = Join-Path $DepsRoot "muparserx\build\Release"
    $muparserObjDir = Join-Path $DepsRoot "muparserx\build\obj-$SimdVariant"
    New-Item -ItemType Directory -Force -Path $muparserBuildDir, $muparserObjDir | Out-Null

    $archArg = if ($SimdVariant -eq "avx") { "/arch:AVX" } else { "" }
    $objects = @()
    $sources = Get-ChildItem -Path $parserDir -Filter "*.cpp" -File | Where-Object { $_.Name -ne "mpTest.cpp" }
    foreach ($source in $sources) {
        $objectPath = Join-Path $muparserObjDir ($source.BaseName + ".obj")
        $clArgs = @("/nologo", "/c", "/EHsc", "/std:c++17", "/O2", "/MD", "/DNDEBUG", "/DMUP_USE_WIDE_STRING", "/I$parserDir", "/Fo$objectPath")
        if ($archArg) {
            $clArgs += $archArg
        }
        $clArgs += $source.FullName
        & cl.exe @clArgs
        if ($LASTEXITCODE -ne 0) {
            throw "muParserX compile failed for $($source.Name)"
        }
        $objects += $objectPath
    }

    $muparserLib = Join-Path $muparserBuildDir "muparserx.lib"
    & lib.exe /nologo "/OUT:$muparserLib" $objects
    if ($LASTEXITCODE -ne 0) {
        throw "muParserX library creation failed"
    }
}

function Install-QtSdk {
    <#
    .SYNOPSIS
        Installs Qt via aqtinstall for the given platform and returns the Qt
        root directory (the folder containing bin\qmake.exe).

    .DESCRIPTION
        Requires python on PATH. Writes aqt-settings.ini under $WorkspaceRoot
        with concurrency 1 (parallel extraction raced intermittently on CI)
        and installs the qtbase/qttools/qtsvg/qttranslations archives only.
        Native tool output is routed to the host so the returned value is just
        the Qt root path.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$WorkspaceRoot,
        [Parameter(Mandatory)] [ValidateSet('x64', 'ARM64')] [string]$Platform,
        [string]$QtVersion
    )

    # Audit #250 F059: the version default lives in simd-variants.psd1 so the
    # CI path (this module) and the local path (setup-build.ps1) cannot drift.
    if (-not $QtVersion) {
        $manifest = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "..\simd-variants.psd1")
        $QtVersion = $manifest.Shared.QtVersion
    }

    $configPath = Join-Path $WorkspaceRoot "aqt-settings.ini"
    Set-Content -Path $configPath -Encoding ASCII -Value @(
        "[aqt]",
        "concurrency: 1"
    )

    # Audit #250 F060: exact pins like every other tool in this pipeline -
    # a floating minor can change extraction/install behaviour under CI.
    python -m pip install --upgrade "setuptools==80.9.0" "py7zr==1.0.0" "aqtinstall==3.2.1" | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install aqtinstall"
    }

    $qtHost = if ($Platform -eq "ARM64") { "windows_arm64" } else { "windows" }
    $qtArch = if ($Platform -eq "ARM64") { "win64_msvc2022_arm64" } else { "win64_msvc2022_64" }
    $qtArchDir = if ($Platform -eq "ARM64") { "msvc2022_arm64" } else { "msvc2022_64" }
    $qtOutput = Join-Path $WorkspaceRoot "Qt"

    # CI runners ship 7z.exe and extraction is faster with it, but local dev
    # machines often lack it; aqt's bundled py7zr handles extraction fine, so
    # only request the external tool when it is actually on PATH.
    $aqtArgs = @(
        '-c', $configPath, 'install-qt', $qtHost, 'desktop', $QtVersion, $qtArch,
        '--autodesktop',
        '--outputdir', $qtOutput,
        '--archives', 'qtbase', 'qttools', 'qtsvg', 'qttranslations'
    )
    if (Get-Command 7z -ErrorAction SilentlyContinue) {
        $aqtArgs += @('--external', '7z')
    }
    python -m aqt @aqtArgs | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Qt installation failed"
    }

    $qtRoot = Join-Path $qtOutput (Join-Path $QtVersion $qtArchDir)
    if (-not (Test-Path (Join-Path $qtRoot "bin\qmake.exe"))) {
        throw "qmake.exe not found under $qtRoot"
    }
    if (-not (Test-Path (Join-Path $qtRoot "include\QtCore\QCoreApplication"))) {
        throw "QtCore headers not found under $qtRoot"
    }

    return $qtRoot
}

Export-ModuleMember -Function Get-SimdVariantEntry, Get-DependencyDownloadSpec, Get-SdkDownloadSpec, Invoke-DependencyDownload, Build-VcpkgDependencies, Install-QtSdk
