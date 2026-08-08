[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)] [ValidateSet("x64", "ARM64")] [string] $Platform,
    [Parameter(Mandatory)] [string] $SimdVariant,
    [switch] $PlanOnly
)

$artifactName = "EqualizerAPO-$Platform-$SimdVariant"
$requiredFiles = @(
    "EqualizerAPO\$Platform\Release\EqualizerAPO.dll",
    "Benchmark\$Platform\Release\Benchmark.exe",
    "VoicemeeterClient\$Platform\Release\VoicemeeterClient.exe"
)
# The standalone MIT Subwoofer Routing VST3 ships inside the same artifact as an
# optional extra: a standard bundle layout under VST3\ plus its own license.
$vst3PluginModule = "VST3\SubwooferRouting\$Platform\Release\EapoXtSubwooferRouting.vst3"
$vst3PluginLicense = "VST3\SubwooferRouting\LICENSE"
$vst3BundleArch = if ($Platform -eq "ARM64") { "arm64-win" } else { "x86_64-win" }
$excludeExtensions = @(".obj", ".res", ".log", ".tlog", ".iobj", ".ipdb", ".ilk", ".pdb")
$plan = [pscustomobject]@{
    ArtifactName = $artifactName
    RequiredFiles = $requiredFiles
    ExcludedExtensions = $excludeExtensions
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
    Get-ChildItem -Path $buildDir -Recurse | Where-Object {
        $_.PSIsContainer -or $excludeExtensions -notcontains $_.Extension.ToLowerInvariant()
    } | ForEach-Object {
        $relative = $_.FullName.Substring($buildDir.Length + 1)
        $target = Join-Path $artifactPath $relative
        if ($_.PSIsContainer) {
            New-Item -ItemType Directory -Force -Path $target | Out-Null
        } else {
            New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $target -Force
        }
    }
}
