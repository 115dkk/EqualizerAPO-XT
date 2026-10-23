<#
.SYNOPSIS
    Prove that physical VST panel edits survive Save and reach the audio engine.
.DESCRIPTION
    The Editor once lost DPF VST3 panel edits by delivering them in a zero-frame
    process call. A parameter API test cannot reproduce that user path. This
    gate uses OS mouse input, compares panel pixels across reopen, parses the
    saved state, and renders that state with the shipped Benchmark.exe.

    Each case opens four panel sessions: baseline, empty-area negative control,
    two control drags, and restore. The negative control must leave the saved
    state and its render exactly as the untouched panel saved them; the edited
    render must differ audibly. The baseline is the untouched panel's save,
    not the bare Library line: a fresh 3 Band EQ filters at 200 Hz until its
    "Low-Mid Freq" (reported as 220 Hz) is written, so a saved full state
    sounds different from the bare line by design (recorded, not gated). The
    ASan leg uses the same path with an instrumented Editor, and any ASan log
    fails the gate.

    Windows PowerShell 5.1 is required for UIAutomationClient. -PlanOnly is pure
    and also runs under PowerShell 7: no filesystem access, assemblies, process
    launch or desktop calls occur before its return. Runtime folders and the
    manifest are required only for execution. Use a fresh WorkDirectory; it is
    never emptied on your behalf. Snapshots retain configs, captures and logs.

    Input timing and desktop discovery are ported from vst-audio's MIT test
    helpers (tests/eapo/native.rs and vst-audio-test-window/src/desktop.rs).
    Coordinates and complete parameter lists refer to DPF-Plugins commit
    df5cb658a2d8dc6e944d8c26a53853a7c0c6c2b2. The signal deliberately uses the
    requested three-second sweep/noise only, not the reference's extra pulses.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [string] $AppDirectory,
    [string] $PluginManifest,
    [string] $WorkDirectory,
    [string] $SnapshotDirectory,
    [string] $AsanLogDirectory,
    [string[]] $Cases,
    [switch] $PlanOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function New-Drag($fromX, $fromY, $toX, $toY) {
    [pscustomobject]@{
        From = [pscustomobject]@{ X = $fromX; Y = $fromY }
        To = [pscustomobject]@{ X = $toX; Y = $toY }
        Steps = 12
    }
}

$allCases = @(
    foreach ($kind in @('eq', 'mverb')) {
        foreach ($format in @('VST2', 'VST3')) {
            $isEq = $kind -eq 'eq'
            [pscustomobject]@{
                Name = "$kind-$($format.ToLowerInvariant())"
                PluginKey = "$($kind.ToUpperInvariant())_$format"
                Format = $format
                WindowClassPrefix = 'DISTRHO-'
                Edits = if ($isEq) { @((New-Drag 82 138 82 98), (New-Drag 208 138 208 168)) } else { @((New-Drag 312 56 312 26), (New-Drag 184 56 184 86)) }
                Background = if ($isEq) { New-Drag 360 100 360 60 } else { New-Drag 20 56 20 86 }
                Expect = if ($isEq) {
                    [pscustomobject]@{ Directions = [ordered]@{ Low = 'rises'; High = 'falls' }; MinChangedParameters = 2; OthersUnchanged = $true }
                } else {
                    [pscustomobject]@{ Directions = [ordered]@{}; MinChangedParameters = 2; OthersUnchanged = $false }
                }
                # Completeness must not mean merely "some state was saved".
                ParameterNames = if ($isEq) {
                    if ($format -eq 'VST2') { @('Low', 'Mid', 'High', 'Master', 'Low-Mid Freq', 'Mid-High Freq') }
                    else { @('low', 'mid', 'high', 'master', 'low_mid', 'mid_high') }
                } else {
                    if ($format -eq 'VST2') { @('Damping', 'Density', 'Bandwidth', 'Decay', 'Predelay', 'Size', 'Gain', 'Mix', 'Early/Late Mix') }
                    else { @('damping', 'density', 'bandwidth', 'decay', 'predelay', 'size', 'gain', 'mix', 'earlymix') }
                }
            }
        }
    }
)
$selected = $allCases
if ($Cases -and $Cases.Count -gt 0) {
    foreach ($name in $Cases) {
        if ($allCases.Name -notcontains $name) { throw "Unknown panel input case '$name'. Choose: $($allCases.Name -join ', ')" }
    }
    $selected = @($allCases | Where-Object { $Cases -contains $_.Name })
}
$plan = [pscustomobject]@{
    Cases = @($selected)
    # Benchmark writes whole --batchsize blocks, so the input is a whole number
    # of them (288 x 512 frames, about 3 s); its output then has the input's length.
    Signal = [pscustomobject]@{ SampleRate = 48000; Channels = 2; Frames = 288 * 512; Format = 'float32'; SweepStartHz = 20; SweepEndHz = 20000; SweepDbfs = -18; NoiseDbfs = -30; Seeds = @('12345678', '9abcdef0') }
    BenchmarkArguments = @('--config-path', '{configDirectory}', '-i', '{inputWav}', '-o', '{outputWav}', '--batchsize', '512', '--nopause')
    MinEditorPixelsChanged = 100
    MinSoundChangeLsb = 33
    AsanOptions = if ($AsanLogDirectory) { 'log_path="' + $AsanLogDirectory.TrimEnd('\', '/') + '\asan":halt_on_error=1' } else { $null }
    Timeouts = [pscustomobject]@{ BenchmarkSeconds = 120; StartupSeconds = 60; StartupDialogsSeconds = 10; PanelSeconds = 10; ButtonSeconds = 15; SettleSeconds = 5; CaptureIntervalMilliseconds = 300; SaveSeconds = 10; ExitSeconds = 30 }
}
if ($PlanOnly) { return $plan }

foreach ($required in @('AppDirectory', 'PluginManifest', 'WorkDirectory', 'SnapshotDirectory')) {
    if ([string]::IsNullOrWhiteSpace((Get-Variable -Name $required -ValueOnly))) { throw "-$required is required unless -PlanOnly" }
}
if ($PSVersionTable.PSEdition -ne 'Desktop' -or $PSVersionTable.PSVersion.Major -ne 5) {
    throw 'Run the panel input gate in Windows PowerShell 5.1 (UIAutomationClient requires .NET Framework).'
}
# Resolve relative CLI paths before changing any child process working directory.
$AppDirectory = [IO.Path]::GetFullPath($AppDirectory)
$PluginManifest = [IO.Path]::GetFullPath($PluginManifest)
$WorkDirectory = [IO.Path]::GetFullPath($WorkDirectory)
$SnapshotDirectory = [IO.Path]::GetFullPath($SnapshotDirectory)
if ($AsanLogDirectory) {
    $AsanLogDirectory = [IO.Path]::GetFullPath($AsanLogDirectory)
    $plan.AsanOptions = 'log_path="' + $AsanLogDirectory.TrimEnd('\') + '\asan":halt_on_error=1'
}
if (Test-Path -LiteralPath $WorkDirectory) {
    if (-not (Test-Path -LiteralPath $WorkDirectory -PathType Container) -or @(Get-ChildItem -LiteralPath $WorkDirectory -Force).Count -ne 0) {
        throw "WorkDirectory must be empty: $WorkDirectory"
    }
}
# Nested snapshot/log roots would make the supposedly fresh work root nonempty.
foreach ($other in @($SnapshotDirectory, $AsanLogDirectory)) {
    if ($other -and ($other.TrimEnd('\') -eq $WorkDirectory.TrimEnd('\') -or $other.StartsWith($WorkDirectory.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase))) {
        throw "Snapshot/ASan directories must be outside WorkDirectory: $other"
    }
}
foreach ($exe in @('Editor.exe', 'Benchmark.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $AppDirectory $exe) -PathType Leaf)) { throw "Missing $exe in $AppDirectory" }
}
$manifest = Get-Content -LiteralPath $PluginManifest -Raw | ConvertFrom-Json
foreach ($case in $plan.Cases) {
    $property = $manifest.PSObject.Properties[$case.PluginKey]
    if ($null -eq $property -or -not [IO.Path]::IsPathRooted([string]$property.Value) -or -not (Test-Path -LiteralPath $property.Value)) {
        throw "Manifest has no existing absolute plugin path for $($case.PluginKey)"
    }
}
New-Item -ItemType Directory -Path $WorkDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $SnapshotDirectory -Force | Out-Null
if ($AsanLogDirectory) { New-Item -ItemType Directory -Path $AsanLogDirectory -Force | Out-Null }
Add-Type -Path (Join-Path $PSScriptRoot 'PanelInputDesktop.cs') -ReferencedAssemblies @('UIAutomationClient', 'UIAutomationTypes', 'WindowsBase', 'System.Drawing')
[PanelInputGate.Desktop]::Initialize()
$utf8 = New-Object System.Text.UTF8Encoding($false)
$summary = [ordered]@{ plan = $plan; checks = @(); failures = @() }
$caseName = 'setup'
$caseSnapshot = $SnapshotDirectory

function Write-Text([string] $path, [string] $text) { [IO.File]::WriteAllText($path, $text, $utf8) }
function Save-Summary { Write-Text (Join-Path $SnapshotDirectory 'summary.json') ($summary | ConvertTo-Json -Depth 14) }
function Add-Check([string] $check, $expected, $observed, [bool] $pass) {
    $summary.checks += [pscustomobject]@{ case = $caseName; check = $check; expected = $expected; observed = $observed; pass = $pass }
    $label = if ($pass) { 'PASS' } else { 'FAIL' }
    Write-Host "[$label] $caseName / $check expected=$expected observed=$observed"
    if (-not $pass) {
        $summary.failures += "$caseName / $check : $observed"
        Capture-Failure ("failure-" + ($check -replace '[^a-zA-Z0-9_-]', '_'))
    }
    Save-Summary
}
function Require-Check([string] $check, $expected, $observed, [bool] $pass) {
    Add-Check $check $expected $observed $pass
    if (-not $pass) { throw "$check : expected $expected; observed $observed" }
}
function Wait-For([string] $label, [int] $seconds, [scriptblock] $action) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        if ($null -ne $editor -and $editor.HasExited) { throw "Editor exited while waiting for $label (exit $($editor.ExitCode))" }
        $value = & $action
        if ($null -ne $value -and $value -ne $false) { return $value }
        Start-Sleep -Milliseconds 100
    } while ($watch.Elapsed.TotalSeconds -lt $seconds)
    throw "Timed out after $seconds seconds: $label"
}
function Click-Rect($rect) {
    [PanelInputGate.Desktop]::Click($editor.Id, [int]($rect.Left + [Math]::Floor($rect.Width / 2)), [int]($rect.Top + [Math]::Floor($rect.Height / 2)))
}
function Click-Named($window, [string] $name) {
    $rect = Wait-For "button '$name' under $($window.Title)" $plan.Timeouts.ButtonSeconds {
        [PanelInputGate.Desktop]::FindNamed($window.Handle, $name, $true)
    }
    Click-Rect $rect
}
function Save-Capture($rect, [string] $name) {
    return ,([PanelInputGate.Desktop]::Capture($rect, (Join-Path $caseSnapshot "$name.png")))
}
function Open-Panel([string] $session) {
    Click-Named $main 'Open panel'
    $panel = Wait-For 'new top-level panel with DISTRHO child' $plan.Timeouts.PanelSeconds {
        foreach ($window in [PanelInputGate.Desktop]::TopWindows($editor.Id)) {
            if ($window.Handle -eq $main.Handle) { continue }
            $child = [PanelInputGate.Desktop]::FindChildClass($window.Handle, $case.WindowClassPrefix)
            if ($null -ne $child) {
                return [pscustomobject]@{ Window = $window; Plugin = $child; Rect = [PanelInputGate.Desktop]::ClientRect($child.Handle) }
            }
        }
    }
    $description = "$session panel=$($panel.Window) plugin=$($panel.Plugin) client=$($panel.Rect)"
    [IO.File]::AppendAllText((Join-Path $caseSnapshot 'windows.log'), "$description`r`n", $utf8)
    Add-Check "$session-window" 'visible DISTRHO child in separate Editor top-level window' $description $true
    foreach ($drag in @($case.Background) + @($case.Edits)) {
        foreach ($point in @($drag.From, $drag.To)) {
            $inside = $point.X -ge 0 -and $point.X -lt $panel.Rect.Width -and $point.Y -ge 0 -and $point.Y -lt $panel.Rect.Height
            Require-Check "$session-coordinate-$($point.X)-$($point.Y)" "inside $($panel.Rect)" "$($point.X),$($point.Y)" $inside
        }
    }
    [PanelInputGate.Desktop]::BringToFront($editor.Id, $panel.Rect.Left + 1, $panel.Rect.Top + 1)
    $previous = [PanelInputGate.Desktop]::Capture($panel.Rect, $null)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $stable = $false
    do {
        Start-Sleep -Milliseconds $plan.Timeouts.CaptureIntervalMilliseconds
        $next = [PanelInputGate.Desktop]::Capture($panel.Rect, $null)
        if ([PanelInputGate.Desktop]::ChangedPixels($previous, $next) -eq 0) { $stable = $true; break }
        $previous = $next
    } while ($watch.Elapsed.TotalSeconds -lt $plan.Timeouts.SettleSeconds)
    Require-Check "$session-settle" 'two equal captures 300 ms apart within 5 s' $stable $stable
    return $panel
}
function Invoke-Drag($panel, $drag) {
    $r = $panel.Rect
    [PanelInputGate.Desktop]::Drag($editor.Id, $r.Left + $drag.From.X, $r.Top + $drag.From.Y, $r.Left + $drag.To.X, $r.Top + $drag.To.Y, $drag.Steps)
    Start-Sleep -Milliseconds 300
}
function Close-Panel($panel, [string] $button) {
    Click-Named $panel.Window $button
    $null = Wait-For "panel closed by $button" $plan.Timeouts.PanelSeconds {
        $found = @([PanelInputGate.Desktop]::TopWindows($editor.Id) | Where-Object { $_.Handle -eq $panel.Window.Handle })
        if ($found.Count -eq 0) { return $true }
    }
}
function Read-PluginLine([string] $path) {
    $lines = @([IO.File]::ReadAllLines($path) | Where-Object { $_ -match '^\s*VSTPlugin:' })
    if ($lines.Count -ne 1) { throw "Expected one VSTPlugin line in $path; found $($lines.Count)" }
    return $lines[0]
}
function Save-Config([string] $session) {
    $before = [IO.File]::ReadAllText($config)
    $stamp = [IO.File]::GetLastWriteTimeUtc($config)
    Click-Named $main 'Save'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $rewritten = $false
    do {
        if ($editor.HasExited) { throw "Editor exited while saving $session" }
        try {
            $now = [IO.File]::ReadAllText($config)
            if ($now -ne $before -or [IO.File]::GetLastWriteTimeUtc($config) -ne $stamp) { $rewritten = $true; break }
        } catch [IO.IOException] { }
        Start-Sleep -Milliseconds 100
    } while ($watch.Elapsed.TotalSeconds -lt $plan.Timeouts.SaveSeconds)
    # Unmodified Save may legitimately do no write. State/audio checks below
    # decide correctness; never substitute invented serialized parameters.
    $observed = if ($rewritten) { 'rewritten' } else { 'unchanged after 10 s; original content retained' }
    Add-Check "$session-save-observation" 'rewrite or unchanged Save (state checked separately)' $observed $true
    $line = Read-PluginLine $config
    Write-Text (Join-Path $caseSnapshot "$session.config.txt") "$line`r`n"
    return $line
}
function Read-State([string] $line) {
    # The current serializer quotes only tokens containing whitespace; accept
    # both spellings (the older Editor quoted every parameter name).
    $libraryMatch = [regex]::Match($line, '^\s*VSTPlugin:\s+Library\s+(?:"([^"]+)"|([^\s"]+))(.*)$')
    if (-not $libraryMatch.Success) { throw 'Saved config omitted the plugin Library path' }
    $savedLibrary = $libraryMatch.Groups[1].Value
    if (-not $savedLibrary) { $savedLibrary = $libraryMatch.Groups[2].Value }
    if (-not [IO.Path]::IsPathRooted($savedLibrary) -or [IO.Path]::GetFullPath($savedLibrary) -ne [IO.Path]::GetFullPath($pluginPath)) { throw "Saved config changed plugin Library path: $savedLibrary" }
    $tail = $libraryMatch.Groups[3].Value
    $map = @{} # PowerShell string keys are case-insensitive, across VST formats.
    if ($case.Format -eq 'VST2') {
        $pair = '^\s+(?:"(?<key>[^"]+)"|(?<key>[^\s"]+))\s+(?<value>[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)(?=\s|$)'
        while (-not [string]::IsNullOrWhiteSpace($tail)) {
            $match = [regex]::Match($tail, $pair)
            if (-not $match.Success) { throw "Malformed VST2 parameter tail: $tail" }
            $key = $match.Groups['key'].Value
            $value = [double]::Parse($match.Groups['value'].Value, [Globalization.CultureInfo]::InvariantCulture)
            if ($map.ContainsKey($key) -or [double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0 -or $value -gt 1) { throw "Invalid/duplicate VST2 parameter '$key'" }
            $map[$key] = $value
            $tail = $tail.Substring($match.Length)
        }
    } else {
        $match = [regex]::Match($tail, '^\s+ChunkData\s+"([A-Za-z0-9+/=]+)"\s*$')
        if (-not $match.Success) { throw 'VST3 state requires a full ChunkData "<base64>" token' }
        $blob = [Convert]::FromBase64String($match.Groups[1].Value)
        if ($blob.Length -ge 4 -and [Text.Encoding]::ASCII.GetString($blob, 0, 4) -eq 'E3ST') {
            if ($blob.Length -lt 20 -or [BitConverter]::ToUInt32($blob, 4) -ne 1) { throw 'Invalid E3ST version/header' }
            $componentSize = [long][BitConverter]::ToUInt32($blob, 8)
            $controllerSize = [long][BitConverter]::ToUInt32($blob, 12)
            $parameterCount = [long][BitConverter]::ToUInt32($blob, 16)
            if (20 + $componentSize + $controllerSize + 12 * $parameterCount -ne $blob.Length -or $componentSize -eq 0) { throw 'Invalid E3ST payload lengths' }
            $component = New-Object byte[] $componentSize
            [Array]::Copy($blob, 20, $component, 0, $componentSize)
            $blob = $component
        }
        # DPF's trailing marker is binary (FE 00), not UTF-8 text. Decode only
        # the NUL-delimited parameter section, leaving the marker untouched.
        $text = [Text.Encoding]::ASCII.GetString($blob)
        $tokens = $text.Split([char]0)
        if ($tokens.Count -lt 6 -or $tokens[0] -ne '__dpf_program__' -or $tokens[1] -notmatch '^\d+$' -or $tokens[2] -ne '__dpf_parameters_begin__') { throw 'Missing DPF program/parameters header' }
        $index = 3
        while ($index -lt $tokens.Count -and $tokens[$index] -ne '__dpf_parameters_end__') {
            if ($index + 1 -ge $tokens.Count) { throw 'Truncated DPF parameter pair' }
            $key = $tokens[$index]
            $value = [double]::Parse($tokens[$index + 1], [Globalization.CultureInfo]::InvariantCulture)
            if (-not $key -or $map.ContainsKey($key) -or [double]::IsNaN($value) -or [double]::IsInfinity($value)) { throw "Invalid/duplicate DPF parameter '$key'" }
            $map[$key] = $value
            $index += 2
        }
        if ($index -ge $tokens.Count -or $tokens[$index] -ne '__dpf_parameters_end__') { throw 'Missing DPF parameters end marker' }
    }
    if ($map.Count -ne $case.ParameterNames.Count) { throw "Incomplete state: expected $($case.ParameterNames.Count) parameters, found $($map.Count)" }
    foreach ($name in $case.ParameterNames) {
        if (-not $map.ContainsKey($name)) { throw "Incomplete state: missing '$name'" }
    }
    return $map
}
function Invoke-Render([string] $name, [string] $line) {
    $dir = Join-Path $caseWork $name
    if (Test-Path -LiteralPath $dir) { throw "Render folder already exists: $dir" }
    New-Item -ItemType Directory -Path $dir | Out-Null
    Write-Text (Join-Path $dir 'config.txt') "$line`r`n"
    Write-Text (Join-Path $caseSnapshot "$name.config.txt") "$line`r`n"
    $output = Join-Path $dir 'out.wav'
    $arguments = @($plan.BenchmarkArguments | ForEach-Object {
        switch ($_ ) {
            '{configDirectory}' { '"' + $dir + '"' }
            '{inputWav}' { '"' + $inputWav + '"' }
            '{outputWav}' { '"' + $output + '"' }
            default { $_ }
        }
    })
    $process = $null
    try {
        $process = Start-Process -FilePath (Join-Path $AppDirectory 'Benchmark.exe') -WorkingDirectory $AppDirectory -ArgumentList $arguments -PassThru -RedirectStandardOutput (Join-Path $caseSnapshot "$name.stdout.log") -RedirectStandardError (Join-Path $caseSnapshot "$name.stderr.log")
        $null = $process.Handle
        $finished = $process.WaitForExit($plan.Timeouts.BenchmarkSeconds * 1000)
        Require-Check "$name-benchmark-timeout" 'exit within 120 s' $finished $finished
        $process.WaitForExit()
        Require-Check "$name-benchmark-exit" 0 $process.ExitCode ($process.ExitCode -eq 0)
        $data = [PanelInputGate.Audio]::ReadPcm16($output, $plan.Signal.SampleRate, $plan.Signal.Channels, $plan.Signal.Frames)
        Add-Check "$name-wav" "48000 Hz stereo PCM16, $($plan.Signal.Frames) frames" "$($data.Length) data bytes" $true
        return ,$data
    } finally {
        if ($null -ne $process) {
            if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
    }
}
function Capture-Failure([string] $name) {
    try { $null = [PanelInputGate.Desktop]::Capture([PanelInputGate.Desktop]::ScreenRect(), (Join-Path $caseSnapshot "$name.png")) }
    catch { Write-Warning "Failure screenshot: $_" }
}

$inputWav = Join-Path $WorkDirectory 'input.wav'
[PanelInputGate.Audio]::WriteSignal($inputWav, $plan.Signal.SampleRate, $plan.Signal.Channels, $plan.Signal.Frames)
Save-Summary
foreach ($case in $plan.Cases) {
    $caseName = $case.Name
    $caseWork = Join-Path $WorkDirectory $caseName
    $caseSnapshot = Join-Path $SnapshotDirectory $caseName
    $editor = $null
    $failureCount = $summary.failures.Count
    try {
        if (Test-Path -LiteralPath $caseWork) { throw "Case folder must not exist: $caseWork" }
        New-Item -ItemType Directory -Path $caseWork | Out-Null
        New-Item -ItemType Directory -Path $caseSnapshot -Force | Out-Null
        $config = Join-Path $caseWork 'config.txt'
        $pluginPath = [string]$manifest.PSObject.Properties[$case.PluginKey].Value
        if ($pluginPath.Contains('"')) { throw 'Plugin path contains a quote' }
        $initial = 'VSTPlugin: Library "' + $pluginPath + '"'
        Write-Text $config "$initial`r`n"
        $defaultAudio = Invoke-Render 'default' $initial

        # Start-Process in 5.1 has no -Environment. Restore the parent's value
        # immediately; only this Editor inherits the ASan options.
        $oldAsan = [Environment]::GetEnvironmentVariable('ASAN_OPTIONS', 'Process')
        try {
            if ($plan.AsanOptions) { $env:ASAN_OPTIONS = $plan.AsanOptions }
            $editor = Start-Process -FilePath (Join-Path $AppDirectory 'Editor.exe') -ArgumentList ('"' + $config + '"') -WorkingDirectory $AppDirectory -PassThru -RedirectStandardOutput (Join-Path $caseSnapshot 'editor.stdout.log') -RedirectStandardError (Join-Path $caseSnapshot 'editor.stderr.log')
            $null = $editor.Handle
        } finally { [Environment]::SetEnvironmentVariable('ASAN_OPTIONS', $oldAsan, 'Process') }
        $main = Wait-For 'Configuration Editor main window' $plan.Timeouts.StartupSeconds {
            foreach ($window in [PanelInputGate.Desktop]::TopWindows($editor.Id)) {
                if ($window.Title.Contains('Configuration Editor')) { return $window }
            }
        }
        # Spend the full startup grace period looking, even after a quiet poll:
        # Registry problem can be followed by another asynchronous message box.
        $startup = [Diagnostics.Stopwatch]::StartNew()
        while ($startup.Elapsed.TotalSeconds -lt $plan.Timeouts.StartupDialogsSeconds) {
            if ($editor.HasExited) { throw "Editor exited during startup: $($editor.ExitCode)" }
            foreach ($window in [PanelInputGate.Desktop]::TopWindows($editor.Id)) {
                if ($window.Handle -eq $main.Handle) { continue }
                $button = [PanelInputGate.Desktop]::FindNamed($window.Handle, 'No', $true)
                $answer = 'No'
                if ($null -eq $button) { $button = [PanelInputGate.Desktop]::FindNamed($window.Handle, 'OK', $true); $answer = 'OK' }
                if ($null -ne $button) {
                    [IO.File]::AppendAllText((Join-Path $caseSnapshot 'message-boxes.log'), "$($window.Title) -> $answer`r`n", $utf8)
                    Click-Rect $button
                    Add-Check 'startup-dialog' 'answer No, else OK' "$($window.Title) -> $answer" $true
                }
            }
            Start-Sleep -Milliseconds 200
        }
        Add-Check 'editor-startup' 'Configuration Editor main window' "$main" $true

        $panel = Open-Panel 'A'
        Close-Panel $panel 'OK'
        $s0 = Save-Config 'S0'
        $state0 = $null
        try {
            $state0 = Read-State $s0
            Add-Check 'S0-full-state' ($case.ParameterNames -join ', ') ($state0.Keys -join ', ') $true
        } catch {
            Add-Check 'S0-full-state' 'an untouched panel''s OK and Save store the full plugin state' "$_" $false
        }

        $panel = Open-Panel 'B'
        $backgroundBefore = Save-Capture $panel.Rect 'background-before'
        Invoke-Drag $panel $case.Background
        $backgroundAfter = Save-Capture $panel.Rect 'background-after'
        $backgroundChanged = [PanelInputGate.Desktop]::ChangedPixels($backgroundBefore, $backgroundAfter)
        Add-Check 'background-pixels' 'informational, not gated' $backgroundChanged $true
        Close-Panel $panel 'OK'
        $s1 = Save-Config 'S1'
        $state1 = Read-State $s1
        Add-Check 'S1-full-state' ($case.ParameterNames -join ', ') ($state1.Keys -join ', ') $true

        $panel = Open-Panel 'C'
        $before = Save-Capture $panel.Rect 'P_before'
        foreach ($drag in $case.Edits) { Invoke-Drag $panel $drag }
        Start-Sleep -Milliseconds 300
        $after = Save-Capture $panel.Rect 'P_after'
        $pixels = [PanelInputGate.Desktop]::ChangedPixels($before, $after)
        Add-Check 'editor-pixels' ">= $($plan.MinEditorPixelsChanged)" $pixels ($pixels -ge $plan.MinEditorPixelsChanged)
        $apply = [PanelInputGate.Desktop]::FindNamed($panel.Window.Handle, 'Apply', $true)
        if ($null -ne $apply) { Click-Rect $apply; Start-Sleep -Milliseconds 500 }
        Add-Check 'panel-apply' 'click Apply if present' ($null -ne $apply) $true
        Close-Panel $panel 'OK'
        $s2 = Save-Config 'S2'
        $state2 = Read-State $s2
        Add-Check 'S2-full-state' ($case.ParameterNames -join ', ') ($state2.Keys -join ', ') $true

        $panel = Open-Panel 'D'
        $reopen = Save-Capture $panel.Rect 'P_reopen'
        $toAfter = [PanelInputGate.Desktop]::ChangedPixels($reopen, $after)
        $toBefore = [PanelInputGate.Desktop]::ChangedPixels($reopen, $before)
        # Closer to the edited picture than to the original one. Not pixel
        # equality: MVerb prints each knob's value as text, and the value it
        # reads back from the host can round to another digit than the one
        # it showed while dragging. A panel that lost the edit matches P_before.
        Add-Check 'panel-restore' 'closer to P_after than to P_before' "after=$toAfter before=$toBefore" ($toAfter -lt $toBefore)
        Close-Panel $panel 'Cancel'

        [PanelInputGate.Desktop]::Close($main.Handle)
        $closing = [Diagnostics.Stopwatch]::StartNew()
        $pending = $false
        while (-not $editor.HasExited -and $closing.Elapsed.TotalSeconds -lt $plan.Timeouts.ExitSeconds) {
            foreach ($window in [PanelInputGate.Desktop]::TopWindows($editor.Id)) {
                foreach ($name in @('No', 'Discard')) {
                    if ($null -ne [PanelInputGate.Desktop]::FindNamed($window.Handle, $name, $true)) {
                        $pending = $true
                        [IO.File]::AppendAllText((Join-Path $caseSnapshot 'message-boxes.log'), "UNSAVED: $($window.Title) [$name]`r`n", $utf8)
                    }
                }
            }
            if ($pending) { break }
            Start-Sleep -Milliseconds 100
        }
        Require-Check 'no-unsaved-prompt' 'no No/Discard dialog after saved config' $pending (-not $pending)
        Require-Check 'editor-close-timeout' 'exit within 30 s' $editor.HasExited $editor.HasExited
        $editor.WaitForExit()
        Add-Check 'editor-exit' 0 $editor.ExitCode ($editor.ExitCode -eq 0)

        $changed = @($state1.Keys | Where-Object { $state1[$_] -ne $state2[$_] })
        if ($case.Format -eq 'VST3') {
            Add-Check 'vst3-panel-edit-saved' 'S2 differs from S1' $(if ($changed.Count -eq 0) { 'panel edit was not saved' } else { $changed -join ', ' }) ($changed.Count -gt 0)
        }
        Add-Check 'changed-parameters' ">= $($case.Expect.MinChangedParameters)" ($changed -join ', ') ($changed.Count -ge $case.Expect.MinChangedParameters)
        foreach ($key in $case.Expect.Directions.Keys) {
            $delta = $state2[$key] - $state1[$key]
            $direction = $case.Expect.Directions[$key]
            $pass = if ($direction -eq 'rises') { $delta -gt 0 } else { $delta -lt 0 }
            Add-Check "parameter-$key" $direction "$($state1[$key]) -> $($state2[$key])" $pass
        }
        if ($case.Expect.OthersUnchanged) {
            $unexpected = @($changed | Where-Object { $case.Expect.Directions.Keys -notcontains $_ })
            Add-Check 'other-parameters-unchanged' 'only Low and High differ' ($unexpected -join ', ') ($unexpected.Count -eq 0)
        }
        if ($null -ne $state0) {
            $moved = @($state0.Keys | Where-Object { $state0[$_] -ne $state1[$_] })
            Add-Check 'background-state' 'S1 equals S0' ($moved -join ', ') ($moved.Count -eq 0)
            $untouchedAudio = Invoke-Render 'S0' $s0
            $savedDifference = [PanelInputGate.Audio]::MaxDifference($defaultAudio, $untouchedAudio)
            Add-Check 'saved-default-sound' 'informational: bare Library line versus the saved untouched state' "max difference $savedDifference LSB" $true
        }
        $baselineAudio = Invoke-Render 'S1' $s1
        $editedAudio = Invoke-Render 'S2' $s2
        if ($null -ne $state0) {
            $negativeDifference = [PanelInputGate.Audio]::MaxDifference($untouchedAudio, $baselineAudio)
            Add-Check 'background-sound' 'S0 and S1 renders identical' "max difference $negativeDifference LSB" ($negativeDifference -eq 0)
        }
        $editDifference = [PanelInputGate.Audio]::MaxDifference($baselineAudio, $editedAudio)
        Add-Check 'edited-sound' ">= $($plan.MinSoundChangeLsb) LSB" $editDifference ($editDifference -ge $plan.MinSoundChangeLsb)
    } catch {
        Add-Check 'scenario' 'all scenario steps complete' ($_ | Out-String) $false
        Capture-Failure 'failure-screen'
    } finally {
        # Preserve the screen before killing a hung Editor, including assertion
        # failures which did not throw so subsequent state/audio checks could run.
        if ($summary.failures.Count -gt $failureCount) { Capture-Failure 'failure-final-screen' }
        if ($null -ne $editor) {
            try { if (-not $editor.HasExited) { $editor.Kill(); $editor.WaitForExit() } }
            catch { Add-Check 'editor-cleanup' 'Editor terminated' "$_" $false }
            finally { $editor.Dispose() }
        }
        try {
            if ($AsanLogDirectory) {
                $asanFiles = @(Get-ChildItem -LiteralPath $AsanLogDirectory -Recurse -File -Force)
                Add-Check 'asan' 'no ASan files' (($asanFiles | ForEach-Object { $_.FullName }) -join ', ') ($asanFiles.Count -eq 0)
                if ($asanFiles.Count -gt 0) {
                    Capture-Failure 'asan-screen'
                    $destination = Join-Path $caseSnapshot 'asan'
                    New-Item -ItemType Directory -Path $destination -Force | Out-Null
                    foreach ($file in $asanFiles) {
                        $relative = $file.FullName.Substring($AsanLogDirectory.TrimEnd('\').Length).TrimStart('\')
                        $target = Join-Path $destination $relative
                        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
                        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
                    }
                }
            }
        } catch { Add-Check 'asan-collection' 'ASan logs checked and preserved' "$_" $false }
        Save-Summary
    }
}
$failed = @($summary.checks | Where-Object { -not $_.pass }).Count
Write-Host "Panel input gate: $($plan.Cases.Count) cases, $($summary.checks.Count) checks, $failed failed. Summary: $(Join-Path $SnapshotDirectory 'summary.json')"
if ($failed -gt 0) { exit 1 }
exit 0
