<#
.SYNOPSIS
    Shared build-dependency provisioning for CI and local setup: pinned
    release-asset download + SHA-256 verification, the vcpkg-based FFTW /
    libsndfile / muparserx rebuild for the sse2/avx variants, and the Qt
    online-repository install.

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
      Install-QtSdk               Qt repository install, returns the Qt root
      Get-PinnedSourceSpec        the header-only source dependencies with their pinned commits
      Sync-PinnedCheckout         clone at a tag, fail unless it is the pinned commit
      Install-PinnedSourceDependency  every source dependency through Sync-PinnedCheckout

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

function Invoke-GitWithRetry {
    # Internal: a git network command with three attempts. A clone that dies
    # half way leaves a directory git refuses to clone into, so CleanupDir is
    # removed before each retry.
    param(
        [Parameter(Mandatory)] [string[]]$Arguments,
        [string]$CleanupDir
    )

    for ($attempt = 1; $attempt -le 3; $attempt++) {
        if ($attempt -gt 1) {
            Write-Host "  git $($Arguments[0]) failed; retrying ($attempt/3) in 10 s..."
            Start-Sleep -Seconds 10
            if ($CleanupDir -and (Test-Path $CleanupDir)) { Remove-Item $CleanupDir -Recurse -Force }
        }
        git @Arguments
        if ($LASTEXITCODE -eq 0) { return }
    }
}

function Invoke-DependencyFetch {
    # Internal: single network fetch, kept separate so tests can mock the
    # network away. Same curl invocation both consumers used inline.
    param(
        [Parameter(Mandatory)] [string]$Url,
        [Parameter(Mandatory)] [string]$OutFile
    )

    # --retry alone skips connection and TLS failures, which are what the
    # hosted runners actually hit; --ssl-revoke-best-effort keeps an
    # unreachable revocation server (CRYPT_E_REVOCATION_OFFLINE) from failing
    # an otherwise valid download.
    curl.exe --fail --location --retry 5 --retry-delay 5 --retry-all-errors --ssl-revoke-best-effort --output $OutFile $Url
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
            Invoke-GitWithRetry -Arguments @('clone', '--depth', '1', 'https://github.com/microsoft/vcpkg', $vcpkgRoot) -CleanupDir $vcpkgRoot
            if ($LASTEXITCODE -ne 0) { throw "Failed to clone vcpkg" }
        }
    }

    # GitHub runner images also provide a git checkout. Pin that checkout just
    # like the fallback clone so CI and local builds use identical portfiles.
    Invoke-GitWithRetry -Arguments @('-C', $vcpkgRoot, 'fetch', '--depth', '1', 'origin', $VcpkgCommit)
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
        $clArgs = @("/nologo", "/c", "/EHsc", "/std:c++20", "/O2", "/MD", "/DNDEBUG", "/DMUP_USE_WIDE_STRING", "/I$parserDir", "/Fo$objectPath")
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

function Get-QtRepositorySpec {
    # Internal: maps the public Platform name to Qt repository/package names.
    # Qt 6.12 introduced a per-architecture repository while older releases
    # use the shared repository, so callers try both URLs in this order.
    param(
        [Parameter(Mandatory)] [string]$QtVersion,
        [Parameter(Mandatory)] [ValidateSet('x64', 'ARM64')] [string]$Platform
    )

    $compactVersion = $QtVersion -replace '\.', ''
    if ($Platform -eq 'ARM64') {
        $hostName = 'windows_arm64'
        $archName = 'win64_msvc2022_arm64'
        $archDir = 'msvc2022_arm64'
    }
    else {
        $hostName = 'windows_x86'
        $archName = 'win64_msvc2022_64'
        $archDir = 'msvc2022_64'
    }

    $repositoryRoot = "https://download.qt.io/online/qtsdkrepository/$hostName/desktop/qt6_$compactVersion"
    return [pscustomobject]@{
        Host           = $hostName
        Arch           = $archName
        ArchDir        = $archDir
        PackageName    = "qt.qt6.$compactVersion.$archName"
        RepositoryUrls = @(
            "$repositoryRoot/qt6_$($compactVersion)_$archDir"
            "$repositoryRoot/qt6_$compactVersion"
        )
    }
}

function Select-QtArchive {
    # Internal and pure: resolves the requested module archives from one
    # Updates.xml document without touching the network or filesystem.
    param(
        [Parameter(Mandatory)] [string]$UpdatesXml,
        [Parameter(Mandatory)] [string]$PackageName,
        [Parameter(Mandatory)] [string]$RepositoryUrl,
        [Parameter(Mandatory)] [string[]]$Modules
    )

    [xml]$document = $UpdatesXml
    $packages = @($document.Updates.PackageUpdate | Where-Object { [string]$_.Name -eq $PackageName })
    if ($packages.Count -eq 0) {
        throw "Qt package $PackageName was not found in Updates.xml"
    }
    if ($packages.Count -ne 1) {
        throw "Qt package $PackageName appears $($packages.Count) times in Updates.xml"
    }

    $package = $packages[0]
    $versionStamp = ([string]$package.Version).Trim()
    $availableArchives = @(
        ([string]$package.DownloadableArchives).Split(',') |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ }
    )

    foreach ($module in $Modules) {
        $prefix = "$module-"
        $candidates = @($availableArchives | Where-Object {
                $_.StartsWith($prefix, [System.StringComparison]::Ordinal)
            })
        if ($candidates.Count -ne 1) {
            throw "Expected one $module archive in Qt package $PackageName, found $($candidates.Count)"
        }

        $archive = $candidates[0]
        [pscustomobject]@{
            Module  = $module
            Archive = $archive
            Url     = "$($RepositoryUrl.TrimEnd('/'))/$PackageName/$versionStamp$archive"
        }
    }
}

function Get-QtRepositoryMetadata {
    # Internal: fetches and verifies Updates.xml. Only a fetch failure falls
    # through to the next layout; a hash mismatch is a hard failure.
    param(
        [Parameter(Mandatory)] [string[]]$RepositoryUrls,
        [Parameter(Mandatory)] [string]$MetadataRoot
    )

    New-Item -ItemType Directory -Force -Path $MetadataRoot | Out-Null
    for ($index = 0; $index -lt $RepositoryUrls.Count; $index++) {
        $repositoryUrl = $RepositoryUrls[$index].TrimEnd('/')
        $xmlPath = Join-Path $MetadataRoot "Updates-$index.xml"
        $shaPath = "$xmlPath.sha256"
        Remove-Item -Path $xmlPath, $shaPath -Force -ErrorAction SilentlyContinue

        try {
            Invoke-DependencyFetch -Url "$repositoryUrl/Updates.xml" -OutFile $xmlPath
            Invoke-DependencyFetch -Url "$repositoryUrl/Updates.xml.sha256" -OutFile $shaPath
        }
        catch {
            Remove-Item -Path $xmlPath, $shaPath -Force -ErrorAction SilentlyContinue
            continue
        }

        $shaText = (Get-Content -Path $shaPath -Raw).Trim()
        $expectedParts = @($shaText -split '\s+')
        $expected = if ($expectedParts.Count -gt 0) { $expectedParts[0].ToLowerInvariant() } else { '' }
        $actual = (Get-FileHash -Path $xmlPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($expected -notmatch '^[0-9a-f]{64}$' -or $actual -ne $expected) {
            throw "SHA-256 mismatch for $repositoryUrl/Updates.xml: expected $expected, got $actual"
        }

        return [pscustomobject]@{
            RepositoryUrl = $repositoryUrl
            UpdatesXml    = Get-Content -Path $xmlPath -Raw
        }
    }

    throw "Failed to fetch Qt Updates.xml from candidate repositories: $($RepositoryUrls -join ', ')"
}

function Get-QtArchive {
    # Internal: downloads or reuses Qt archives and verifies each one before
    # Install-QtSdk extracts anything. The manifest version uses reviewed
    # SHA-256 pins; an explicit version override uses Qt's SHA-1 sidecars.
    param(
        [Parameter(Mandatory)] [object[]]$Archives,
        [Parameter(Mandatory)] [string]$DownloadRoot,
        [Parameter(Mandatory)] [string]$QtVersion,
        [Parameter(Mandatory)] [ValidateSet('x64', 'ARM64')] [string]$Platform,
        [Parameter(Mandatory)] [hashtable]$Manifest
    )

    New-Item -ItemType Directory -Force -Path $DownloadRoot | Out-Null
    $usePins = $QtVersion -eq [string]$Manifest.Shared.QtVersion
    $platformPins = $null
    if ($usePins -and $Manifest.Shared.QtArchiveSha256) {
        $platformPins = $Manifest.Shared.QtArchiveSha256[$Platform]
    }
    if (-not $usePins) {
        Write-Warning "Qt $QtVersion $Platform archives are not pinned in simd-variants.psd1; verifying Qt repository SHA-1 sidecars."
    }

    foreach ($archiveSpec in $Archives) {
        $archivePath = Join-Path $DownloadRoot $archiveSpec.Archive
        $expected = $null
        if ($usePins -and $platformPins) {
            $expected = [string]$platformPins[$archiveSpec.Module]
            if ($expected) { $expected = $expected.ToLowerInvariant() }
        }

        if ($usePins) {
            if ($expected -and (Test-Path $archivePath)) {
                $cachedHash = (Get-FileHash -Path $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($cachedHash -eq $expected) {
                    Write-Host "  [cached] $($archiveSpec.Archive)"
                    Write-Host "Observed SHA-256 for $($archiveSpec.Archive): $cachedHash"
                    [pscustomobject]@{
                        Module  = $archiveSpec.Module
                        Archive = $archiveSpec.Archive
                        Path    = $archivePath
                    }
                    continue
                }
                Remove-Item -Path $archivePath -Force
            }
            elseif (Test-Path $archivePath) {
                # A missing pin must report the bytes currently served, rather
                # than accepting a cache entry from an earlier invocation.
                Remove-Item -Path $archivePath -Force
            }

            Write-Host "Downloading Qt archive $($archiveSpec.Archive)"
            Invoke-DependencyFetch -Url $archiveSpec.Url -OutFile $archivePath
            $actual = (Get-FileHash -Path $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
            Write-Host "Observed SHA-256 for $($archiveSpec.Archive): $actual"
            if (-not $expected) {
                throw "No SHA-256 pin for Qt $QtVersion $Platform $($archiveSpec.Module) in simd-variants.psd1 (observed $actual)"
            }
            if ($actual -ne $expected) {
                Remove-Item -Path $archivePath -Force
                throw "SHA-256 mismatch for $($archiveSpec.Archive): expected $expected, got $actual"
            }
        }
        else {
            $sha1Path = "$archivePath.sha1"
            Invoke-DependencyFetch -Url "$($archiveSpec.Url).sha1" -OutFile $sha1Path
            $sha1Text = (Get-Content -Path $sha1Path -Raw).Trim()
            $sha1Parts = @($sha1Text -split '\s+')
            $expected = if ($sha1Parts.Count -gt 0) { $sha1Parts[0].ToLowerInvariant() } else { '' }
            if ($expected -notmatch '^[0-9a-f]{40}$') {
                throw "Invalid SHA-1 sidecar for $($archiveSpec.Archive): $sha1Text"
            }

            $cached = $false
            if (Test-Path $archivePath) {
                $cachedHash = (Get-FileHash -Path $archivePath -Algorithm SHA1).Hash.ToLowerInvariant()
                if ($cachedHash -eq $expected) {
                    Write-Host "  [cached] $($archiveSpec.Archive)"
                    $cached = $true
                }
                else {
                    Remove-Item -Path $archivePath -Force
                }
            }
            if (-not $cached) {
                Write-Host "Downloading Qt archive $($archiveSpec.Archive)"
                Invoke-DependencyFetch -Url $archiveSpec.Url -OutFile $archivePath
                $actual = (Get-FileHash -Path $archivePath -Algorithm SHA1).Hash.ToLowerInvariant()
                if ($actual -ne $expected) {
                    Remove-Item -Path $archivePath -Force
                    throw "SHA-1 mismatch for $($archiveSpec.Archive): expected $expected, got $actual"
                }
            }
        }

        [pscustomobject]@{
            Module  = $archiveSpec.Module
            Archive = $archiveSpec.Archive
            Path    = $archivePath
        }
    }
}

function Install-QtSdk {
    <#
    .SYNOPSIS
        Installs Qt from Qt's online repository for the given platform and
        returns the Qt root directory (the folder containing bin\qmake.exe).

    .DESCRIPTION
        Reads and verifies Updates.xml, selects the qtbase/qtsvg/qttools/
        qttranslations archives, verifies their manifest pins, and extracts
        them directly. aqtinstall is not used because it cannot read the Qt
        6.12 repository layout. Native tool output is routed to the host so the
        returned value is just the Qt root path.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$WorkspaceRoot,
        [Parameter(Mandatory)] [ValidateSet('x64', 'ARM64')] [string]$Platform,
        [string]$QtVersion
    )

    $manifest = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "..\simd-variants.psd1")
    # Audit #250 F059: the version default lives in simd-variants.psd1 so the
    # CI path (this module) and the local path (setup-build.ps1) cannot drift.
    if (-not $QtVersion) {
        $QtVersion = $manifest.Shared.QtVersion
    }

    $repositorySpec = Get-QtRepositorySpec -QtVersion $QtVersion -Platform $Platform
    $archiveRoot = Join-Path $WorkspaceRoot ("Qt\_archives\$QtVersion-$Platform")
    $metadata = Get-QtRepositoryMetadata -RepositoryUrls $repositorySpec.RepositoryUrls `
        -MetadataRoot (Join-Path $archiveRoot '_metadata')
    $modules = @('qtbase', 'qtsvg', 'qttools', 'qttranslations')
    $archiveSpecs = @(Select-QtArchive -UpdatesXml $metadata.UpdatesXml `
            -PackageName $repositorySpec.PackageName -RepositoryUrl $metadata.RepositoryUrl `
            -Modules $modules)
    $archives = @(Get-QtArchive -Archives $archiveSpecs -DownloadRoot $archiveRoot `
            -QtVersion $QtVersion -Platform $Platform -Manifest $manifest)

    $qtRoot = Join-Path $WorkspaceRoot ("Qt\$QtVersion\$($repositorySpec.ArchDir)")
    New-Item -ItemType Directory -Force -Path $qtRoot | Out-Null
    $sevenZip = Get-Command 7z -ErrorAction SilentlyContinue
    if (-not $sevenZip) {
        python -m pip install "py7zr==1.0.0" | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to install py7zr"
        }
    }

    foreach ($archive in $archives) {
        Write-Host "Extracting $($archive.Archive) -> $qtRoot"
        if ($sevenZip) {
            & $sevenZip.Source x -y "-o$qtRoot" $archive.Path | Out-Host
        }
        else {
            python -m py7zr x $archive.Path $qtRoot | Out-Host
        }
        if ($LASTEXITCODE -ne 0) {
            throw "Qt archive extraction failed for $($archive.Archive)"
        }
    }

    $qtConfigPath = Join-Path $qtRoot 'bin\qt.conf'
    Set-Content -Path $qtConfigPath -Encoding ASCII -Value @('[Paths]', 'Prefix=..')

    $qmakePath = Join-Path $qtRoot "bin\qmake.exe"
    if (-not (Test-Path $qmakePath)) {
        throw "qmake.exe not found under $qtRoot"
    }
    if (-not (Test-Path (Join-Path $qtRoot "include\QtCore\QCoreApplication"))) {
        throw "QtCore headers not found under $qtRoot"
    }

    $canRunQmake = $Platform -eq 'x64' -or ($Platform -eq 'ARM64' -and $env:PROCESSOR_ARCHITECTURE -eq 'ARM64')
    if ($canRunQmake) {
        $reportedVersion = (& $qmakePath -query QT_VERSION | Out-String).Trim()
        if ($LASTEXITCODE -ne 0) {
            throw "qmake.exe version query failed under $qtRoot"
        }
        if ($reportedVersion -ne $QtVersion) {
            throw "qmake.exe under $qtRoot reports Qt $reportedVersion, expected $QtVersion"
        }
    }

    return $qtRoot
}

function Get-PinnedSourceSpec {
    <#
    .SYNOPSIS
        The three header-only source dependencies, each with the tag it is
        cloned at and the commit that tag must resolve to (from the manifest).
    #>
    param(
        [Parameter(Mandatory)] [hashtable] $Manifest,
        [Parameter(Mandatory)] [string] $DepsRoot
    )
    $shared = $Manifest.Shared
    @(
        [pscustomobject]@{ Name = 'TCLAP'; RepoUrl = 'https://github.com/115dkk/tclap'
            Tag = $shared.TclapTag; Commit = $shared.TclapCommit
            CheckoutDir = (Join-Path $DepsRoot 'tclap'); ProbeFile = 'include' },
        [pscustomobject]@{ Name = 'VST3 pluginterfaces'; RepoUrl = 'https://github.com/steinbergmedia/vst3_pluginterfaces'
            Tag = $shared.Vst3Tag; Commit = $shared.Vst3Commit
            CheckoutDir = (Join-Path $DepsRoot 'vst3sdk\pluginterfaces'); ProbeFile = 'base\funknown.h' },
        [pscustomobject]@{ Name = 'Highway'; RepoUrl = 'https://github.com/google/highway'
            Tag = $shared.HighwayTag; Commit = $shared.HighwayCommit
            CheckoutDir = (Join-Path $DepsRoot 'highway'); ProbeFile = 'hwy\highway.h' }
    )
}

function Sync-PinnedCheckout {
    <#
    .SYNOPSIS
        Clones one source dependency at its tag and fails unless the tag
        resolved to the manifest's commit; a cached checkout at that commit
        is kept.
    .DESCRIPTION
        A git tag is movable. Audit #250 F072/F058 gave local setup this
        check; CI still checked the three repositories out by tag alone, so a
        retargeted upstream tag would have changed the shipped binaries with
        no diff here (audit #348 TD-26). CI and setup-build.ps1 both call this
        now.
    #>
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $RepoUrl,
        [Parameter(Mandatory)] [string] $Tag,
        [Parameter(Mandatory)] [string] $ExpectedCommit,
        [Parameter(Mandatory)] [string] $CheckoutDir,
        [Parameter(Mandatory)] [string] $ProbeFile
    )

    $cachedCommit = $null
    if ((Test-Path (Join-Path $CheckoutDir $ProbeFile)) -and (Test-Path (Join-Path $CheckoutDir ".git"))) {
        $cachedCommit = (git -C $CheckoutDir rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -ne 0) { $cachedCommit = $null; $global:LASTEXITCODE = 0 }
    }
    if ($cachedCommit -eq $ExpectedCommit -and $cachedCommit) {
        Write-Host "  [cached] $Name already present at $Tag ($ExpectedCommit)"
        return
    }

    if (Test-Path $CheckoutDir) {
        Write-Host "  Cached $Name is not at $Tag/$ExpectedCommit; re-cloning..."
        Remove-Item $CheckoutDir -Recurse -Force
    }
    $parent = Split-Path -Parent $CheckoutDir
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    Invoke-GitWithRetry -Arguments @('clone', '--depth', '1', '--branch', $Tag, $RepoUrl, $CheckoutDir) -CleanupDir $CheckoutDir
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone $Name" }

    $actualCommit = (git -C $CheckoutDir rev-parse HEAD)
    if ($actualCommit -ne $ExpectedCommit) {
        throw "$Name tag $Tag resolved to $actualCommit, but the manifest pins $ExpectedCommit. The tag moved - review the diff and update simd-variants.psd1 deliberately."
    }
    Write-Host "  -> $CheckoutDir ($actualCommit)"
}

function Install-PinnedSourceDependency {
    <#
    .SYNOPSIS
        Every source dependency from Get-PinnedSourceSpec, through
        Sync-PinnedCheckout.
    #>
    param(
        [Parameter(Mandatory)] [hashtable] $Manifest,
        [Parameter(Mandatory)] [string] $DepsRoot
    )
    foreach ($spec in (Get-PinnedSourceSpec -Manifest $Manifest -DepsRoot $DepsRoot)) {
        Sync-PinnedCheckout -Name $spec.Name -RepoUrl $spec.RepoUrl -Tag $spec.Tag `
            -ExpectedCommit $spec.Commit -CheckoutDir $spec.CheckoutDir -ProbeFile $spec.ProbeFile
    }
}

Export-ModuleMember -Function Get-SimdVariantEntry, Get-DependencyDownloadSpec, Get-SdkDownloadSpec, Invoke-DependencyDownload, Build-VcpkgDependencies, Install-QtSdk, Get-PinnedSourceSpec, Sync-PinnedCheckout, Install-PinnedSourceDependency
