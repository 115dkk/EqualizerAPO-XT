<#
.SYNOPSIS
    The one description of the MSBuild projects and of the msbuild
    arguments every build script passes.

.DESCRIPTION
    Audit #422 T1. Build-Solution.ps1, Invoke-MemcheckTests.ps1,
    Invoke-CodeQLBuild.ps1, Build-AsioWin32.ps1 and the VST3 preview probe
    workflow each carried their own project list and their own copy of the
    /p: parameter set. They now select from Get-EapoProjectGraph by tag and
    build through Get-EapoMsBuildArguments / Invoke-EapoMsBuild.

    The graph is ordered: a project comes after everything it links or
    loads, so building a selection in graph order is always correct.
    Test-SourceSync.ps1 holds EqualizerAPO.sln to the same project set.

    Tags:
      engine     the static libraries every consumer links
      fixture    test plug-ins and drivers the suites and probes load
      plugin     a shipped plug-in module
      suite      a test executable CI runs
      product    a shipped program or DLL
      probe      an executable a CI gate drives
      solution   built by Build-Solution.ps1 on every leg
      win32      also built for Win32 (the 32-bit ASIO wrapper family)
      installer  the Win32-only auto-detect installer
#>

$script:Graph = @(
    @{ Name = "SubwooferRoutingCore";     Path = "SubwooferRoutingCore\SubwooferRoutingCore.vcxproj";            Tags = @("engine", "solution") }
    @{ Name = "Common";                   Path = "Common.vcxproj";                                               Tags = @("engine", "solution") }
    @{ Name = "TestVst2Plugin";           Path = "Tests\TestVst2Plugin\TestVst2Plugin.vcxproj";                  Tags = @("fixture", "solution") }
    # Built through HybridConvTests' ProjectReference on the solution legs.
    @{ Name = "TestVst3Plugin";           Path = "Tests\TestVst3Plugin\TestVst3Plugin.vcxproj";                  Tags = @("fixture") }
    @{ Name = "SubwooferRoutingVst3";     Path = "VST3\SubwooferRouting\SubwooferRoutingVst3.vcxproj";           Tags = @("plugin", "product", "solution") }
    @{ Name = "HybridConvTests";          Path = "Tests\HybridConvTests\HybridConvTests.vcxproj";                Tags = @("suite", "solution") }
    @{ Name = "EditorLogicTests";         Path = "Tests\EditorLogicTests\EditorLogicTests.vcxproj";              Tags = @("suite", "solution") }
    @{ Name = "EngineOrchestrationTests"; Path = "Tests\EngineOrchestrationTests\EngineOrchestrationTests.vcxproj"; Tags = @("suite", "solution") }
    @{ Name = "AudioRegressionTests";     Path = "Tests\AudioRegressionTests\AudioRegressionTests.vcxproj";      Tags = @("suite", "solution") }
    @{ Name = "EqualizerAPO";             Path = "EqualizerAPO\EqualizerAPO.vcxproj";                            Tags = @("product", "solution") }
    @{ Name = "Benchmark";                Path = "Benchmark\Benchmark.vcxproj";                                  Tags = @("product", "solution") }
    @{ Name = "VoicemeeterClient";        Path = "VoicemeeterClient\VoicemeeterClient.vcxproj";                  Tags = @("product", "solution") }
    @{ Name = "VstPreviewProbe";          Path = "Tests\VstPreviewProbe\VstPreviewProbe.vcxproj";                Tags = @("probe", "solution") }
    @{ Name = "EqualizerAPOAsio";         Path = "EqualizerAPOAsio\EqualizerAPOAsio.vcxproj";                    Tags = @("product", "solution", "win32") }
    @{ Name = "EqualizerAPOHost";         Path = "EqualizerAPOHost\EqualizerAPOHost.vcxproj";                    Tags = @("product", "solution") }
    @{ Name = "FakeAsioDriver";           Path = "Tests\FakeAsioDriver\FakeAsioDriver.vcxproj";                  Tags = @("fixture", "solution", "win32") }
    @{ Name = "AsioTests";                Path = "Tests\AsioTests\AsioTests.vcxproj";                            Tags = @("suite", "solution") }
    @{ Name = "AsioProbe";                Path = "Tests\AsioProbe\AsioProbe.vcxproj";                            Tags = @("probe", "solution") }
    @{ Name = "ApoHostProbe";             Path = "Tests\ApoHostProbe\ApoHostProbe.vcxproj";                      Tags = @("probe", "solution") }
    @{ Name = "CaptureProbe";             Path = "Tests\CaptureProbe\CaptureProbe.vcxproj";                      Tags = @("probe", "solution") }
    @{ Name = "Installer";                Path = "Installer\Installer.vcxproj";                                  Tags = @("installer", "product") }
)

function Get-EapoProjectGraph {
    <#
    .SYNOPSIS
        The projects in build order, optionally filtered. -Tag keeps a
        project carrying any of the tags; -Name keeps the named projects.
    #>
    [CmdletBinding()]
    param(
        [string[]] $Tag,
        [string[]] $Name
    )
    foreach ($entry in $script:Graph) {
        if ($Tag -and -not ($entry.Tags | Where-Object { $Tag -contains $_ })) { continue }
        if ($Name -and $Name -notcontains $entry.Name) { continue }
        [pscustomobject]@{ Name = $entry.Name; Path = $entry.Path; Tags = $entry.Tags }
    }
}

function Get-EapoMsBuildArguments {
    <#
    .SYNOPSIS
        The msbuild arguments shared by every build script.

    .DESCRIPTION
        Dependency paths are passed only when the matching environment
        variable is set. A global /p:VST3_SDK= with an empty value would
        otherwise override Directory.Build.props' default and leave the
        include path empty, which is what used to happen on a developer
        machine that had not exported every variable.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [ValidateSet("x64", "ARM64", "Win32")] [string] $Platform,
        [string] $Configuration = "Release",
        [string] $Target = "rebuild",
        [AllowEmptyString()] [string] $ArchFlag,
        [string] $PlatformToolset = "v145",
        [string] $ToolArchitecture,
        # All: include and library paths. Includes: header paths only (a
        # compile-only build). None: no dependency paths (projects that
        # link none of them, such as the installer).
        [ValidateSet("All", "Includes", "None")] [string] $Dependencies = "All",
        [string[]] $Only
    )
    $arguments = @("/m", "/p:Configuration=$Configuration", "/p:Platform=$Platform", "/t:$Target")
    $includes = @("LIBSNDFILE_INCLUDE", "FFTW_INCLUDE", "MUPARSERX_INCLUDE", "TCLAP_ROOT", "VST3_SDK", "HIGHWAY_INCLUDE", "ASIO_SDK", "QT_ROOT")
    $libraries = @("LIBSNDFILE_LIB", "FFTW_LIB", "MUPARSERX_LIB")
    $names = switch ($Dependencies) { "All" { $includes + $libraries } "Includes" { $includes } "None" { @() } }
    if ($Only) { $names = @($names | Where-Object { $Only -contains $_ }) }
    foreach ($name in $names) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) { $arguments += "/p:$name=$value" }
    }
    $arguments += "/p:PlatformToolset=$PlatformToolset"
    if ($ToolArchitecture) { $arguments += "/p:PreferredToolArchitecture=$ToolArchitecture" }
    if ($Platform -eq "x64" -and $ArchFlag) { $arguments += "/p:EnableEnhancedInstructionSet=$ArchFlag" }
    $arguments
}

function Invoke-EapoMsBuild {
    <#
    .SYNOPSIS
        Builds each project in order with the same arguments; throws on the
        first failure, naming the project.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string[]] $Project,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [string] $FailureLabel = "Build"
    )
    foreach ($path in $Project) {
        msbuild $path @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$FailureLabel failed for $path" }
    }
}

Export-ModuleMember -Function Get-EapoProjectGraph, Get-EapoMsBuildArguments, Invoke-EapoMsBuild
