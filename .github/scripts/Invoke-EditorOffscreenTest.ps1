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

    One gate is not offscreen: frame-hittest asks real Win32 windows for
    WM_NCHITTEST answers, so it runs on the windows platform plugin (QtPlatform
    in its entry). It sends the messages itself and never needs the desktop
    pointer, so other windows on the runner's desktop cannot disturb it.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [Parameter(Mandatory)] [string] $Gate,
    [string] $Platform = "x64",
    # Where deps\ lives; a git worktree has none of its own. Defaults to the
    # workspace root, which is the layout CI and setup-build.ps1 create.
    [string] $DepsRoot,
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"
if (-not $DepsRoot) { $DepsRoot = $WorkspaceRoot }

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
            QT_FORCE_STDERR_LOGGING   = "1"
        }
        # The gallery reports its findings as qWarning lines; without a log a
        # failing run says only its exit code.
        LogPath    = Join-Path $WorkspaceRoot "skin-gallery.log"
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
    "plugin-load" = [pscustomobject]@{
        Arguments  = @("--plugin-load-test", (Join-Path $WorkspaceRoot "plugin-load\config.txt"))
        ExtraEnv   = @{
            QT_FORCE_STDERR_LOGGING         = "1"
            EAPO_PLUGIN_LOAD_GAP_LIMIT_MS   = "2000"
        }
        LogPath    = Join-Path $WorkspaceRoot "plugin-load\plugin-load-test.log"
        PostChecks = @()
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
        PostChecks = @("card-selection-shots")
    }
    # View > Interface: the two modern presentations with a skin each, the
    # Legacy editor entry and its restart, the Ctrl+Alt+1..5 shortcuts, and
    # that a choice made in the check never reaches the saved preferences.
    "interface-menu" = [pscustomobject]@{
        Arguments  = @("--interface-menu-test")
        ExtraEnv   = @{ QT_FORCE_STDERR_LOGGING = "1" }
        LogPath    = Join-Path $WorkspaceRoot "interface-menu-test.log"
        PostChecks = @()
    }
    "power-toggle" = [pscustomobject]@{
        Arguments  = @("--power-toggle-test")
        ExtraEnv   = @{ QT_FORCE_STDERR_LOGGING = "1" }
        LogPath    = Join-Path $WorkspaceRoot "power-toggle-test.log"
        PostChecks = @()
    }
    "routing-edit" = [pscustomobject]@{
        Arguments  = @("--routing-edit-test")
        ExtraEnv   = @{ QT_FORCE_STDERR_LOGGING = "1" }
        LogPath    = Join-Path $WorkspaceRoot "routing-edit-test.log"
        PostChecks = @()
    }
    "close-save" = [pscustomobject]@{
        Arguments  = @("--close-save-test")
        ExtraEnv   = @{ QT_FORCE_STDERR_LOGGING = "1" }
        LogPath    = Join-Path $WorkspaceRoot "close-save-test.log"
        PostChecks = @()
    }
    # Every gallery card row times every edit its body offers, saved through
    # the close prompt and through Ctrl+S. Two passes, so a failure that
    # comes and goes has two chances to show; a pass took 71 s locally.
    "close-save-sweep" = [pscustomobject]@{
        Arguments  = @("--close-save-sweep", "2")
        ExtraEnv   = @{ QT_FORCE_STDERR_LOGGING = "1" }
        LogPath    = Join-Path $WorkspaceRoot "close-save-sweep.log"
        PostChecks = @()
    }
    # A plug-in that writes its state as a fresh archive on every read and
    # calls automate while its state is written back (Kilohearts, #459) must
    # not mark the document unsaved; a real change must still do so. The VST2
    # fixture plays that plug-in through EAPO_TEST_VST_STATE, which the gate
    # sets itself for each case.
    "vst-untouched" = [pscustomobject]@{
        Arguments  = @("--vst-untouched-test")
        ExtraEnv   = @{
            QT_FORCE_STDERR_LOGGING  = "1"
            EAPO_GALLERY_VST2_PLUGIN = Join-Path $WorkspaceRoot "Tests\TestVst2Plugin\$Platform\Release\TestVst2Plugin.dll"
        }
        LogPath    = Join-Path $WorkspaceRoot "vst-untouched-test.log"
        PostChecks = @()
    }
    # The custom frame under a native child cascade (an embedded VST panel
    # turns the chrome host into a child HWND): caption and edges must stay
    # draggable (diagnostics/FrameHitTestProbe).
    "frame-hittest" = [pscustomobject]@{
        Arguments  = @("--frame-hittest-test", (Join-Path $WorkspaceRoot "Setup\config\config.txt"))
        ExtraEnv   = @{ QT_FORCE_STDERR_LOGGING = "1" }
        QtPlatform = "windows"
        LogPath    = Join-Path $WorkspaceRoot "frame-hittest-test.log"
        PostChecks = @()
    }
}

# The gallery again in each shipped translation a headless runner can draw.
# Its layout checks (a card wider than the list, a legend that does not fit
# its cap) depend on how long the translated strings are, and the English run
# cannot see that: the French filter card was wider than the list in all five
# skins and nothing reported it. Simplified Chinese is left out because the
# offscreen platform has no CJK face and draws every character as a box.
$translatedGalleryLanguages = @("fr", "de", "ko")
foreach ($language in $translatedGalleryLanguages) {
    $translated = [pscustomobject]@{
        Arguments  = @("--skin-gallery", (Join-Path $WorkspaceRoot "skin-gallery-$language"))
        ExtraEnv   = @{
            EAPO_GALLERY_VST3_PLUGIN  = $gates["skin-gallery"].ExtraEnv["EAPO_GALLERY_VST3_PLUGIN"]
            EAPO_GALLERY_VST2_PLUGIN  = $gates["skin-gallery"].ExtraEnv["EAPO_GALLERY_VST2_PLUGIN"]
            EAPO_GALLERY_LANG         = $language
            QT_FORCE_STDERR_LOGGING   = "1"
        }
        LogPath    = Join-Path $WorkspaceRoot "skin-gallery-$language.log"
        PostChecks = @("gallery-not-empty")
    }
    $gates["skin-gallery-$language"] = $translated
}

# The list checks again in Modern rows. A row and a card hold the same editors
# in a different layout, and a check that passes in one can fail in the other:
# the save sweep found a Send line rewritten without an edit only in rows,
# where one of its steps reached a control that a card does not expose.
#
# - A check that walks every skin runs rows in the skins that draw them and
#   cards in the rest, so its row run widens by itself as skins gain rows.
# - A check that opens one Editor window takes the skin saved on the machine,
#   and a runner has none saved: it would open the default skin, which may
#   have no rows, and pass in cards. Those checks name their window's skin.
# - Either way the Editor prints which skins drew rows and exits non-zero
#   when none did; rows-coverage below reads that line from the log, so a
#   check that ends the process on a path of its own cannot pass without it.
#
# Each row run has its own log and output folder, so it never replaces the
# card run's proof before the upload.
$rowListGates = @("power-toggle", "routing-edit", "close-save", "close-save-sweep", "skin-switch", "card-move", "card-selection")
$rowWindowGates = @("close-save", "close-save-sweep")
$rowWindowSkin = "rack"
foreach ($name in $rowListGates) {
    $base = $gates[$name]
    $outDir = Join-Path $WorkspaceRoot $name
    $arguments = @($base.Arguments | ForEach-Object { if ($_ -eq $outDir) { "$outDir-rows" } else { $_ } })
    $arguments += @("--list-mode", "rows")
    if ($rowWindowGates -contains $name) {
        $arguments += @("--window-skin", $rowWindowSkin)
    }
    $logPath = if ($base.LogPath.StartsWith("$outDir\")) {
        Join-Path "$outDir-rows" (Split-Path $base.LogPath -Leaf)
    } else {
        $base.LogPath -replace '\.log$', '-rows.log'
    }
    $gates["$name-rows"] = [pscustomobject]@{
        Arguments  = $arguments
        ExtraEnv   = $base.ExtraEnv
        LogPath    = $logPath
        PostChecks = @($base.PostChecks) + @("rows-coverage")
    }
}

if (-not $gates.ContainsKey($Gate)) {
    $validGates = @($gates.Keys | Sort-Object)
    throw "Unknown Editor offscreen gate '$Gate'. Valid gates: $($validGates -join ', ')"
}

$spec = $gates[$Gate]
$plan = [pscustomobject]@{
    Gate        = $Gate
    EditorExe   = $editorExe
    Arguments   = @($spec.Arguments)
    EditorFlags = @($spec.Arguments | Where-Object { $_ -is [string] -and $_.StartsWith("--") })
    ExtraEnv   = $spec.ExtraEnv
    LogPath    = $spec.LogPath
    PostChecks = @($spec.PostChecks)
    QtPlatform = if ($spec.PSObject.Properties["QtPlatform"]) { $spec.QtPlatform } else { "offscreen" }
    TranslatedGalleryLanguages = @($translatedGalleryLanguages)
    RowListGates = @($rowListGates)
    RowWindowGates = @($rowWindowGates)
    RowWindowSkin = $rowWindowSkin
    # The import lib embeds the SONAME velopack_libc.dll (see Package-
    # Artifacts); the gates run from the build dir, so the renamed DLL must
    # sit next to Editor.exe. Idempotent, so every gate does it.
    VelopackDllSource = Join-Path $DepsRoot "deps\velopack_libc\lib\velopack_libc_win_${Platform}_msvc.dll".ToLowerInvariant()
}
if ($PlanOnly) {
    return $plan
}

if (-not (Test-Path $editorExe)) {
    throw "Editor executable not found at $editorExe"
}

if ($Gate -eq "plugin-load") {
    $pluginLoadDir = Join-Path $WorkspaceRoot "plugin-load"
    New-Item -ItemType Directory -Force -Path $pluginLoadDir | Out-Null
    $fixture = Join-Path $WorkspaceRoot "Tests\TestVst2Plugin\$Platform\Release\TestVst2Plugin.dll"
    if (-not (Test-Path $fixture)) {
        throw "VST load fixture not found: $fixture"
    }
    $slowPlugin = Join-Path $pluginLoadDir "TestVst2Plugin.SlowLoad4000.dll"
    Copy-Item $fixture -Destination $slowPlugin -Force
    Set-Content -LiteralPath (Join-Path $pluginLoadDir "config.txt") `
        -Value "VSTPlugin: Library `"$slowPlugin`"" -Encoding utf8NoBOM
}

Copy-Item $plan.VelopackDllSource -Destination (Join-Path $buildDir "velopack_libc.dll") -Force

# Shared headless preamble. QT_QPA_PLATFORM_PLUGIN_PATH must point at the
# full Qt install: windeployqt deploys only qwindows next to Editor.exe, and
# the deployed Qt6Core.dll wins the DLL search, so without this the offscreen
# plugin fails to load and Qt parks a fatal-error dialog forever.
# A fresh shell on a prepared checkout has neither the dependency paths nor
# QT_ROOT; without them the Editor exits 0xC0000135 (a DLL it cannot find).
# Derive both from the checkout, as Build-Solution.ps1 does.
if (-not $env:FFTW_LIB -and (Test-Path (Join-Path $DepsRoot "deps"))) {
    & (Join-Path $PSScriptRoot "Initialize-DepsEnvironment.ps1") -DepsPath (Join-Path $DepsRoot "deps") | Out-Null
}
if (-not $env:QT_ROOT) {
    Import-Module (Join-Path $PSScriptRoot "Provisioning.psm1") -Force
    $gateManifest = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "..\simd-variants.psd1")
    $gateVariant = Resolve-BuildVariant -Manifest $gateManifest -Platform $Platform -Simd ""
    $env:QT_ROOT = Join-Path $DepsRoot "Qt\$($gateManifest.Shared.QtVersion)\$($gateVariant.QtArchDir)"
}
$env:PATH = "$env:FFTW_LIB;$env:LIBSNDFILE_LIB;$env:QT_ROOT\bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$env:QT_ROOT\plugins\platforms"
$env:QT_QPA_PLATFORM = $plan.QtPlatform
foreach ($name in $spec.ExtraEnv.Keys) {
    Set-Item -Path "env:$name" -Value $spec.ExtraEnv[$name]
}

if ($Gate -like "skin-gallery*") {
    foreach ($fixture in @($env:EAPO_GALLERY_VST3_PLUGIN, $env:EAPO_GALLERY_VST2_PLUGIN)) {
        if (-not (Test-Path $fixture)) {
            throw "VST gallery fixture not found: $fixture"
        }
    }
    $env:EAPO_GALLERY_VST3_UPMIXER = Join-Path ([System.IO.Path]::GetTempPath()) "Upmixer.vst3"
    if ($env:RUNNER_TEMP) { $env:EAPO_GALLERY_VST3_UPMIXER = Join-Path $env:RUNNER_TEMP "Upmixer.vst3" }
    Copy-Item $env:EAPO_GALLERY_VST3_PLUGIN -Destination $env:EAPO_GALLERY_VST3_UPMIXER -Force
}

if ($Gate -eq "vst-untouched" -and -not (Test-Path $env:EAPO_GALLERY_VST2_PLUGIN)) {
    throw "VST fixture not found: $env:EAPO_GALLERY_VST2_PLUGIN"
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
        "rows-coverage" {
            $coverage = @(Get-Content $spec.LogPath | Where-Object { $_ -match '^ListMode: rows in ' }) |
                Select-Object -Last 1
            if (-not $coverage) {
                throw "Gate '$Gate' did not report which skins drew rows"
            }
            if ($coverage -match '^ListMode: rows in no skin') {
                throw "Gate '$Gate' ran without a skin that draws rows: $coverage"
            }
            Write-Host "Row coverage: $coverage"
        }
        "card-selection-shots" {
            # Audit #422 TD-41: the gate exits 0 after its own checks, but an
            # empty proof directory must not pass as a proof.
            $outDir = $plan.Arguments[1]
            $pngs = @(Get-ChildItem $outDir -Filter *_card-selection.png -ErrorAction SilentlyContinue)
            Write-Host "Card selection wrote $($pngs.Count) PNGs"
            if ($pngs.Count -lt 1) {
                throw "Card selection test produced no screenshots"
            }
        }
    }
}
