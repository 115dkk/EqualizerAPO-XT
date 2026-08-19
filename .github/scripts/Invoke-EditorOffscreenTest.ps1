<#
.SYNOPSIS
    Runs one of the Editor's offscreen gates (self-tests and screenshot
    galleries) with the shared headless preamble.

.DESCRIPTION
    Extracted from six inline build.yml steps (audit #275 D6/TD-24): every
    gate repeated the same preamble - the velopack_libc SONAME copy, the
    dependency PATH, the QT_QPA offscreen setup with the platform-plugin
    search pointed at the full Qt install (without which the offscreen plugin
    fails to load and Qt parks a fatal-error dialog forever on a headless
    runner), stderr log capture for a GUI-subsystem binary, and the
    Start-Process exit-code collection. The knowledge lived in six copies;
    now it lives here once, and adding a gate is a table entry instead of a
    YAML block.

    The plan phase is pure: -PlanOnly answers which arguments, environment
    additions and post-checks a gate would run with, for Pester.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)]
    [ValidateSet("selftest-vst", "skin-gallery", "skin-switch", "analysis-layout", "card-move", "card-selection")]
    [string] $Gate,
    [string] $Platform = "x64",
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"

$buildDir = Join-Path $WorkspaceRoot "build-Editor-$Platform\release"
$editorExe = Join-Path $buildDir "Editor.exe"

# Per-gate table. Env values are resolved lazily at execution; the plan lists
# names and static values so tests can pin them.
$gates = @{
    "selftest-vst" = [pscustomobject]@{
        Arguments  = @("--selftest-vst")
        ExtraEnv   = @{}
        LogPath    = $null
        PostChecks = @()
    }
    "skin-gallery" = [pscustomobject]@{
        Arguments  = @("--skin-gallery", (Join-Path $WorkspaceRoot "skin-gallery"))
        # Deterministic VST bus scenes against the actually loaded ABI: the
        # solution's test plugins provide accepted / auto / rejected VST3
        # states and the VST2 stale-keys warning. A copy named Upmixer.vst3
        # flips TestVst3Plugin into its Stereo -> 7.1 mode (filename-driven).
        ExtraEnv   = @{
            EAPO_GALLERY_VST3_PLUGIN  = Join-Path $WorkspaceRoot "Tests\TestVst3Plugin\$Platform\Release\TestVst3Plugin.vst3"
            EAPO_GALLERY_VST2_PLUGIN  = Join-Path $WorkspaceRoot "Tests\TestVst2Plugin\$Platform\Release\TestVst2Plugin.dll"
        }
        LogPath    = $null
        PostChecks = @("gallery-not-empty")
    }
    "skin-switch" = [pscustomobject]@{
        Arguments  = @("--skin-switch-test")
        # Keep the measured 100+ row card rebuild within a useful regression
        # budget (see the historical notes in git for the 8s -> 5s tightening).
        ExtraEnv   = @{
            QT_FORCE_STDERR_LOGGING = "1"
            EAPO_SWITCH_WARN_MS     = "2500"
            EAPO_SWITCH_LIMIT_MS    = "5000"
        }
        LogPath    = Join-Path $WorkspaceRoot "skin-switch-test.log"
        PostChecks = @()
    }
    "analysis-layout" = [pscustomobject]@{
        Arguments  = @(
            "--analysis-layout-test",
            (Join-Path $WorkspaceRoot "analysis-layout\right-dock.png"),
            (Join-Path $WorkspaceRoot "Setup\config\config.txt"))
        ExtraEnv   = @{ QT_FORCE_STDERR_LOGGING = "1" }
        LogPath    = Join-Path $WorkspaceRoot "analysis-layout\analysis-layout-test.log"
        PostChecks = @("analysis-screenshot")
    }
    "card-move" = [pscustomobject]@{
        Arguments  = @("--card-move-test")
        # The limit sits below the measured full-rebuild cost on the hosted
        # runner (1.4-1.6 s per move): a return to the rebuild fails the gate
        # while leaving the incremental path 6x headroom for slow days.
        ExtraEnv   = @{
            QT_FORCE_STDERR_LOGGING = "1"
            EAPO_MOVE_WARN_MS       = "250"
            EAPO_MOVE_LIMIT_MS      = "1000"
        }
        LogPath    = Join-Path $WorkspaceRoot "card-move-test.log"
        PostChecks = @()
    }
    "card-selection" = [pscustomobject]@{
        Arguments  = @("--card-selection-test", (Join-Path $WorkspaceRoot "card-selection"))
        ExtraEnv   = @{ QT_FORCE_STDERR_LOGGING = "1" }
        LogPath    = Join-Path $WorkspaceRoot "card-selection\card-selection-test.log"
        PostChecks = @()
    }
}

$spec = $gates[$Gate]
$plan = [pscustomobject]@{
    Gate       = $Gate
    EditorExe  = $editorExe
    Arguments  = @($spec.Arguments)
    ExtraEnv   = $spec.ExtraEnv
    LogPath    = $spec.LogPath
    PostChecks = @($spec.PostChecks)
    # The import lib embeds the SONAME velopack_libc.dll (see Package-
    # Artifacts); the gates run from the build dir, so the renamed DLL must
    # sit next to Editor.exe. Idempotent, so every gate does it.
    VelopackDllSource = Join-Path $WorkspaceRoot "deps\velopack_libc\lib\velopack_libc_win_${Platform}_msvc.dll".ToLowerInvariant()
}
if ($PlanOnly) {
    return $plan
}

if (-not (Test-Path $editorExe)) {
    throw "Editor executable not found at $editorExe"
}

Copy-Item $plan.VelopackDllSource -Destination (Join-Path $buildDir "velopack_libc.dll") -Force

# Shared headless preamble. QT_QPA_PLATFORM_PLUGIN_PATH must point at the
# full Qt install: windeployqt deploys only qwindows next to Editor.exe, and
# the deployed Qt6Core.dll wins the DLL search, so without this the offscreen
# plugin fails to load and Qt parks a fatal-error dialog forever.
$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$env:QT_ROOT\bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$env:QT_ROOT\plugins\platforms"
$env:QT_QPA_PLATFORM = "offscreen"
foreach ($name in $spec.ExtraEnv.Keys) {
    Set-Item -Path "env:$name" -Value $spec.ExtraEnv[$name]
}

if ($Gate -eq "skin-gallery") {
    foreach ($fixture in @($env:EAPO_GALLERY_VST3_PLUGIN, $env:EAPO_GALLERY_VST2_PLUGIN)) {
        if (-not (Test-Path $fixture)) {
            throw "VST gallery fixture not found: $fixture"
        }
    }
    $env:EAPO_GALLERY_VST3_UPMIXER = Join-Path ([System.IO.Path]::GetTempPath()) "Upmixer.vst3"
    if ($env:RUNNER_TEMP) { $env:EAPO_GALLERY_VST3_UPMIXER = Join-Path $env:RUNNER_TEMP "Upmixer.vst3" }
    Copy-Item $env:EAPO_GALLERY_VST3_PLUGIN -Destination $env:EAPO_GALLERY_VST3_UPMIXER -Force
}

if ($spec.LogPath) {
    New-Item -ItemType Directory -Force -Path (Split-Path $spec.LogPath -Parent) | Out-Null
}

# Editor.exe is a GUI-subsystem binary; Start-Process -Wait is the reliable
# way to collect its exit code from PowerShell, and stderr only carries
# qWarning output when redirected (with QT_FORCE_STDERR_LOGGING where set).
$startArgs = @{
    FilePath     = $editorExe
    ArgumentList = $plan.Arguments
    PassThru     = $true
    Wait         = $true
    NoNewWindow  = $true
}
if ($spec.LogPath) {
    $startArgs.RedirectStandardError = $spec.LogPath
}
$proc = Start-Process @startArgs

if ($spec.LogPath -and (Test-Path $spec.LogPath)) {
    Get-Content $spec.LogPath | Write-Host
}
if ($proc.ExitCode -ne 0) {
    throw "Editor offscreen gate '$Gate' failed (exit code $($proc.ExitCode))"
}

foreach ($check in $plan.PostChecks) {
    switch ($check) {
        "gallery-not-empty" {
            $outDir = $plan.Arguments[1]
            $pngs = @(Get-ChildItem $outDir -Filter *.png)
            Write-Host "Gallery wrote $($pngs.Count) PNGs"
            # SkinGallery self-checks the exact shot count and exits non-zero
            # on a mismatch; this only guards against an empty run.
            if ($pngs.Count -lt 1) {
                throw "Gallery produced no PNGs"
            }
        }
        "analysis-screenshot" {
            $shot = $plan.Arguments[1]
            if (-not (Test-Path $shot)) {
                throw "Analysis dock layout test produced no screenshot"
            }
        }
    }
}
