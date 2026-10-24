<#
.SYNOPSIS
    Builds pinned DPF test plugins outside the checkout for the real Editor gate.

.DESCRIPTION
    The gate drives the 3 Band EQ and MVerb editors with real OS input. Their
    sources and binaries stay outside the checkout, and pinned Mesa llvmpipe
    DLLs provide OpenGL on GPU-less hosted runners. Mesa is installed beside
    each plugin binary and in any additional process directories supplied by
    the caller, because Editor loads opengl32.dll before it loads a plugin.

    -PlanOnly returns the source, Mesa, plugin and manifest decisions without
    requiring -Root or touching the file system.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    # Build root outside the checkout: sources, the CMake tree, Mesa. Required unless -PlanOnly.
    [string] $Root,
    # Further folders that receive Mesa's two DLLs, e.g. the folder Editor.exe runs from:
    # the Editor process loads opengl32.dll before any plugin, so the plugin binds to that copy.
    [string[]] $MesaDestination = @(),
    [switch] $PlanOnly
)

$sources = @(
    [pscustomobject]@{
        Name = "DPF"
        Url = "https://github.com/DISTRHO/DPF.git"
        Commit = "b801214e9ce41b56907e1c6c84859b6b4536917d"
        Submodules = @("dgl/src/pugl-upstream")
    },
    [pscustomobject]@{
        Name = "DPF-Plugins"
        Url = "https://github.com/DISTRHO/DPF-Plugins.git"
        Commit = "df5cb658a2d8dc6e944d8c26a53853a7c0c6c2b2"
        Submodules = @()
    }
)
$mesa = [pscustomobject]@{
    Url = "https://github.com/pal1000/mesa-dist-win/releases/download/26.2.4/mesa3d-26.2.4-release-msvc.7z"
    Sha256 = "351FC8C8B695878FFB3EAA044B3EAD08672A48B1A045E3C3E3975811DF0F6695"
    Files = @("x64/opengl32.dll", "x64/libgallium_wgl.dll")
}
$plugins = [ordered]@{
    EQ_VST2 = "dpf-build\bin\d_3BandEQ-vst2.dll"
    EQ_VST3 = "dpf-build\bin\d_3BandEQ.vst3"
    MVERB_VST2 = "dpf-build\bin\d_MVerb-vst2.dll"
    MVERB_VST3 = "dpf-build\bin\d_MVerb.vst3"
}
$plan = [pscustomobject]@{
    Sources = $sources
    Mesa = $mesa
    Plugins = $plugins
    ManifestName = "dpf-plugins.json"
}
if ($PlanOnly) { return $plan }

$ErrorActionPreference = "Stop"
if (-not $Root) { throw "-Root is required unless -PlanOnly is used" }

$workspaceFull = [System.IO.Path]::GetFullPath($WorkspaceRoot)
$rootFull = [System.IO.Path]::GetFullPath($Root)
$relativeToWorkspace = [System.IO.Path]::GetRelativePath($workspaceFull, $rootFull)
$rootIsInWorkspace = $relativeToWorkspace -eq "." -or
    (-not [System.IO.Path]::IsPathRooted($relativeToWorkspace) -and
        $relativeToWorkspace -ne ".." -and
        -not $relativeToWorkspace.StartsWith("..$([System.IO.Path]::DirectorySeparatorChar)"))
if ($rootIsInWorkspace) {
    throw "The DPF build root must be outside the workspace: $rootFull"
}
New-Item -ItemType Directory -Force -Path $rootFull | Out-Null

function Invoke-NativeCommand([string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory) {
    Push-Location $WorkingDirectory
    try {
        & $Executable @Arguments 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Native command failed ($LASTEXITCODE): $Executable $($Arguments -join ' ')"
        }
    }
    finally { Pop-Location }
}

function Invoke-NativeCapture([string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory) {
    Push-Location $WorkingDirectory
    try {
        $output = & $Executable @Arguments 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "Native command failed ($LASTEXITCODE): $Executable $($Arguments -join ' ')"
        }
        return ($output -join "`n").Trim()
    }
    finally { Pop-Location }
}

$sourceDirectories = @{}
foreach ($source in $sources) {
    $directoryName = $source.Name.ToLowerInvariant()
    $sourceDirectory = Join-Path $rootFull $directoryName
    if (-not (Test-Path -LiteralPath $sourceDirectory)) {
        Invoke-NativeCommand -Executable "git" `
            -Arguments @("clone", "--filter=blob:none", "--no-checkout", $source.Url, $sourceDirectory) `
            -WorkingDirectory $rootFull
    }
    Invoke-NativeCommand -Executable "git" -Arguments @("checkout", "--detach", $source.Commit) `
        -WorkingDirectory $sourceDirectory
    $actualCommit = Invoke-NativeCapture -Executable "git" -Arguments @("rev-parse", "HEAD") `
        -WorkingDirectory $sourceDirectory
    if ($actualCommit -ne $source.Commit) {
        throw "$($source.Name) checkout is at $actualCommit instead of $($source.Commit)"
    }
    if ($source.Submodules.Count) {
        Invoke-NativeCommand -Executable "git" `
            -Arguments (@("submodule", "update", "--init", "--depth", "1") + $source.Submodules) `
            -WorkingDirectory $sourceDirectory
    }
    $status = Invoke-NativeCapture -Executable "git" -Arguments @("status", "--porcelain") `
        -WorkingDirectory $sourceDirectory
    if ($status) { throw "$($source.Name) source tree is dirty: $status" }
    $sourceDirectories[$source.Name] = $sourceDirectory
}

$dpfPath = $sourceDirectories["DPF"].Replace("\", "/")
$dpfPluginsPath = $sourceDirectories["DPF-Plugins"].Replace("\", "/")
$wrapperDirectory = Join-Path $rootFull "dpf-project"
New-Item -ItemType Directory -Force -Path $wrapperDirectory | Out-Null
$cmakeLists = @"
cmake_minimum_required(VERSION 3.25)
project(eapo_dpf_test_plugins LANGUAGES C CXX)
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)
set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("$dpfPath" dpf)
dpf_add_plugin(d_3BandEQ TARGETS vst2 vst3 FILES_DSP "$dpfPluginsPath/plugins/3BandEQ/DistrhoPlugin3BandEQ.cpp" FILES_UI "$dpfPluginsPath/plugins/3BandEQ/DistrhoArtwork3BandEQ.cpp" "$dpfPluginsPath/plugins/3BandEQ/DistrhoUI3BandEQ.cpp")
target_include_directories(d_3BandEQ PUBLIC "$dpfPluginsPath/plugins/3BandEQ")
dpf_add_plugin(d_MVerb TARGETS vst2 vst3 FILES_DSP "$dpfPluginsPath/plugins/MVerb/DistrhoPluginMVerb.cpp" FILES_UI "$dpfPluginsPath/plugins/MVerb/DistrhoArtworkMVerb.cpp" "$dpfPluginsPath/plugins/MVerb/DistrhoUIMVerb.cpp")
target_include_directories(d_MVerb PUBLIC "$dpfPluginsPath/plugins/MVerb")
"@
Set-Content -LiteralPath (Join-Path $wrapperDirectory "CMakeLists.txt") -Value $cmakeLists -Encoding utf8

$buildDirectory = Join-Path $rootFull "dpf-build"
Invoke-NativeCommand -Executable "cmake" `
    -Arguments @("-S", $wrapperDirectory, "-B", $buildDirectory, "-A", "x64") `
    -WorkingDirectory $rootFull
Invoke-NativeCommand -Executable "cmake" `
    -Arguments @("--build", $buildDirectory, "--config", "Release", "--parallel") `
    -WorkingDirectory $rootFull

$absolutePlugins = [ordered]@{}
foreach ($entry in $plugins.GetEnumerator()) {
    $path = Join-Path $rootFull $entry.Value
    if (-not (Test-Path -LiteralPath $path)) { throw "DPF plugin was not produced: $path" }
    $absolutePlugins[$entry.Key] = [System.IO.Path]::GetFullPath($path)
}

$archive = Join-Path $rootFull "mesa.7z"
$download = "$archive.download"
for ($attempt = 1; $attempt -le 3; $attempt++) {
    try {
        Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue
        Invoke-WebRequest -UseBasicParsing -Uri $mesa.Url -OutFile $download
        Move-Item -LiteralPath $download -Destination $archive -Force
        break
    }
    catch {
        Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue
        if ($attempt -eq 3) { throw }
        Start-Sleep -Seconds 2
    }
}
$actualDigest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToUpperInvariant()
if ($actualDigest -ne $mesa.Sha256) {
    throw "Mesa SHA-256 mismatch: expected $($mesa.Sha256), got $actualDigest"
}

$sevenZipCommand = Get-Command "7z" -ErrorAction SilentlyContinue
$sevenZip = if ($sevenZipCommand) { $sevenZipCommand.Source } else { "C:\Program Files\7-Zip\7z.exe" }
if (-not $sevenZipCommand -and -not (Test-Path -LiteralPath $sevenZip)) {
    throw "Could not locate 7-Zip as 7z on PATH or C:\Program Files\7-Zip\7z.exe"
}
$mesaDirectory = Join-Path $rootFull "mesa"
New-Item -ItemType Directory -Force -Path $mesaDirectory | Out-Null
Invoke-NativeCommand -Executable $sevenZip `
    -Arguments (@("x", "-y", "-o$mesaDirectory", $archive) + $mesa.Files) `
    -WorkingDirectory $rootFull

$pluginDestinations = @(
    Split-Path -Parent $absolutePlugins.EQ_VST2
    Join-Path $absolutePlugins.EQ_VST3 "Contents\x86_64-win"
    Split-Path -Parent $absolutePlugins.MVERB_VST2
    Join-Path $absolutePlugins.MVERB_VST3 "Contents\x86_64-win"
)
$destinations = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($destination in @($pluginDestinations) + @($MesaDestination)) {
    if (-not (Test-Path -LiteralPath $destination -PathType Container)) {
        throw "Mesa destination does not exist: $destination"
    }
    [void] $destinations.Add([System.IO.Path]::GetFullPath($destination))
}
foreach ($destination in $destinations) {
    foreach ($file in $mesa.Files) {
        $sourceFile = Join-Path $mesaDirectory $file
        if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
            throw "Mesa archive did not contain $file"
        }
        Copy-Item -LiteralPath $sourceFile -Destination $destination -Force
    }
}

$manifest = Join-Path $rootFull $plan.ManifestName
$absolutePlugins | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding utf8
return [pscustomobject]@{
    EQ_VST2 = $absolutePlugins.EQ_VST2
    EQ_VST3 = $absolutePlugins.EQ_VST3
    MVERB_VST2 = $absolutePlugins.MVERB_VST2
    MVERB_VST3 = $absolutePlugins.MVERB_VST3
    Manifest = $manifest
}
