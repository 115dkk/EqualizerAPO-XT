[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)] [ValidateSet("x64", "ARM64")] [string] $Platform,
    [Parameter(Mandatory)] [string] $SimdVariant,
    [string] $ManifestPath = (Join-Path $PSScriptRoot "..\simd-variants.psd1"),
    [switch] $PlanOnly
)

$artifactName = "EqualizerAPO-$Platform-$SimdVariant"
$requiredFiles = @(
    "EqualizerAPO\$Platform\Release\EqualizerAPO.dll",
    "Benchmark\$Platform\Release\Benchmark.exe",
    "VoicemeeterClient\$Platform\Release\VoicemeeterClient.exe",
    # ASIO (docs/architecture/asio-host-study.md): the wrapper driver a DAW
    # loads and the engine host it starts on demand.
    "EqualizerAPOAsio\$Platform\Release\EqualizerAPOAsio.dll",
    "EqualizerAPOHost\$Platform\Release\EqualizerAPOHost.exe"
)
# 32-bit DAWs load a 32-bit driver: the Win32 wrapper ships under the x86
# folder on the x64 legs (Build-AsioWin32.ps1 builds it there). The ARM64 leg
# has no x86 cross-build and ships without it; AsioAPOInfo registers the
# 64-bit view only when the file is absent.
$win32Wrapper = if ($Platform -eq "x64") { "EqualizerAPOAsio\Release\EqualizerAPOAsio.dll" } else { $null }
$extraDllFolders = @("x86")
# The standalone MIT Subwoofer Routing VST3 ships inside the same artifact as an
# optional extra: a standard bundle layout under VST3\ plus its own license.
$vst3PluginModule = "VST3\SubwooferRouting\$Platform\Release\EapoXtSubwooferRouting.vst3"
$vst3PluginLicense = "VST3\SubwooferRouting\LICENSE"
$vst3BundleArch = if ($Platform -eq "ARM64") { "arm64-win" } else { "x86_64-win" }
# Everything the Qt build leaves in release\ that is not part of the program:
# object files and symbols, and the precompiled headers, moc/rcc sources and
# their headers that qmake writes next to the executable. From v2.38.0 to
# v2.48.0 the recursive copy below shipped those (three .pch files alone were
# 887 MB unpacked), which made every installer ~250 MB instead of ~65.
$excludeExtensions = @(".obj", ".res", ".log", ".tlog", ".iobj", ".ipdb", ".ilk", ".pdb",
    ".pch", ".cpp", ".h", ".c", ".hpp", ".moc", ".exp", ".lib")
# The Qt plugin folder list lives in the manifest (Shared.QtPluginFolders) so
# the packaging assertion below and New-VelopackRelease.ps1's qt\ relocation
# cannot drift apart (audit #275 TD-11: generic/ and networkinformation/ were
# deployed by windeployqt but silently missing from releases).
$qtPluginFolders = @((Import-PowerShellDataFile $ManifestPath).Shared.QtPluginFolders)
$plan = [pscustomobject]@{
    ArtifactName = $artifactName
    RequiredFiles = $requiredFiles
    Win32Wrapper = $win32Wrapper
    ExcludedExtensions = $excludeExtensions
    QtPluginFolders = $qtPluginFolders
}
if ($PlanOnly) { return $plan }

$ErrorActionPreference = "Stop"
$artifactPath = Join-Path $WorkspaceRoot "artifacts\$artifactName"
New-Item -ItemType Directory -Force -Path $artifactPath | Out-Null
foreach ($relative in $requiredFiles) {
    $source = Join-Path $WorkspaceRoot $relative
    if (-not (Test-Path $source)) { throw "Required file not found: $relative" }
    Copy-Item $source -Destination $artifactPath
}
# The license texts the binaries are conveyed under: GPLv2-or-later for the
# program, GPLv3 for the ASIO wrapper built on the Steinberg SDK. GPLv2
# section 1 and GPLv3 section 4 want a copy with every distribution.
foreach ($license in @("License.txt", "License-gpl-3.0.txt")) {
    Copy-Item (Join-Path $WorkspaceRoot $license) -Destination $artifactPath -Force
}
if ($win32Wrapper) {
    $win32Source = Join-Path $WorkspaceRoot $win32Wrapper
    if (-not (Test-Path $win32Source)) { throw "Required file not found: $win32Wrapper" }
    New-Item -ItemType Directory -Force -Path (Join-Path $artifactPath "x86") | Out-Null
    Copy-Item $win32Source -Destination (Join-Path $artifactPath "x86") -Force
}
Copy-Item (Join-Path $WorkspaceRoot "deps\fftw\Release\*.dll") -Destination $artifactPath -Force
Copy-Item (Join-Path $WorkspaceRoot "deps\libsndfile\build\Release\*.dll") -Destination $artifactPath -Force
$velopackArch = if ($Platform -eq "ARM64") { "arm64" } else { "x64" }
Copy-Item (Join-Path $WorkspaceRoot "deps\velopack_libc\lib\velopack_libc_win_${velopackArch}_msvc.dll") `
    -Destination (Join-Path $artifactPath "velopack_libc.dll") -Force

$vst3Source = Join-Path $WorkspaceRoot $vst3PluginModule
if (-not (Test-Path $vst3Source)) { throw "Required file not found: $vst3PluginModule" }
$vst3BundleDir = Join-Path $artifactPath "VST3\EapoXtSubwooferRouting.vst3\Contents\$vst3BundleArch"
New-Item -ItemType Directory -Force -Path $vst3BundleDir | Out-Null
Copy-Item $vst3Source -Destination (Join-Path $vst3BundleDir "EapoXtSubwooferRouting.vst3") -Force
Copy-Item (Join-Path $WorkspaceRoot $vst3PluginLicense) `
    -Destination (Join-Path $artifactPath "VST3\EapoXtSubwooferRouting.vst3\LICENSE") -Force

foreach ($app in @("Editor", "DeviceSelector", "UpdateChecker")) {
    $buildDir = Join-Path $WorkspaceRoot "build-$app-$Platform\release"
    $exe = Join-Path $buildDir "$app.exe"
    if (-not (Test-Path $exe)) { throw "$app.exe not built" }
    # Files only: directories materialize with their first file, so the
    # object-mirror folders (whose contents the extension filter drops
    # entirely) never appear as empty directories in the artifact. That keeps
    # the artifact directory uploadable as a whole.
    Get-ChildItem -Path $buildDir -Recurse -File | Where-Object {
        $excludeExtensions -notcontains $_.Extension.ToLowerInvariant()
    } | ForEach-Object {
        $relative = $_.FullName.Substring($buildDir.Length + 1)
        $target = Join-Path $artifactPath $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
}

# Belt and braces for the filter above: no build intermediate may be in the
# artifact whatever extension a future qmake layout gives it. Named by the
# patterns qmake uses, so a new kind shows up here as a failed release job
# rather than as a 250 MB installer.
$intermediates = @(Get-ChildItem -Path $artifactPath -Recurse -File | Where-Object {
    $_.Name -like "moc_*" -or $_.Name -like "qrc_*" -or $_.Name -like "*_pch.*" -or $_.Name -like "*.pch"
})
if ($intermediates.Count -gt 0) {
    throw ("Build intermediates in the artifact: " + (($intermediates | Select-Object -First 5 | ForEach-Object { $_.Name }) -join ", ") +
        ". Extend the exclusion list in Package-Artifacts.ps1.")
}

# Every DLL-carrying folder in the artifact must be a known Qt plugin folder
# (VST3\ carries the .vst3 bundle, not DLLs). When windeployqt starts emitting
# a folder this manifest list does not know, fail here - the alternative is a
# release where that plugin family is silently absent on user machines.
$unknownDllFolders = @(Get-ChildItem -Path $artifactPath -Directory | Where-Object {
    $qtPluginFolders -notcontains $_.Name -and $extraDllFolders -notcontains $_.Name -and
    @(Get-ChildItem -Path $_.FullName -Recurse -File -Filter "*.dll").Count -gt 0
} | ForEach-Object { $_.Name })
if ($unknownDllFolders.Count -gt 0) {
    throw ("Artifact folders carry DLLs but are not in Shared.QtPluginFolders " +
        "(.github/simd-variants.psd1): $($unknownDllFolders -join ', '). " +
        "Add them to the manifest so releases relocate them under qt\.")
}
