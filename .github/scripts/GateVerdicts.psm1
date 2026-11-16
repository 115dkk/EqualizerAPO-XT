# Pure decisions shared by the measured gates and their fake-input tests.
# Keep Windows PowerShell 5.1 syntax: the panel gate uses UIAutomationClient.
Set-StrictMode -Version Latest

function Get-CaptureGateVerdict([string[]] $Failures) {
    [pscustomobject]@{ Passed = @($Failures).Count -eq 0; Reasons = @($Failures) }
}

function Get-GateFileVerdict([bool] $Exists, [string] $Message) {
    $reasons = @()
    if (-not $Exists) { $reasons += $Message }
    Get-CaptureGateVerdict $reasons
}

function Get-CaptureMeasurementVerdict($Record, [bool] $RoundRequired) {
    # CaptureProbe owns gain/tolerance evaluation. Re-evaluating the rounded
    # JSON gain here would disagree at the tolerance boundary (audit #467 TD-67).
    $passed = $Record.exitCode -eq 0
    $smallPeriodAvailable = $null
    $noSmallPeriod = $false
    $note = $Record.note
    # A "min" request that came back at the default period says the driver
    # declares no smaller one: nothing to judge, and the summary says so.
    if ($Record.period -eq 'min' -and $null -ne $Record.engineMinPeriodFrames -and $null -ne $Record.engineDefaultPeriodFrames) {
        $smallPeriodAvailable = [int]$Record.engineMinPeriodFrames -lt [int]$Record.engineDefaultPeriodFrames
        $noSmallPeriod = -not $smallPeriodAvailable
        if ($noSmallPeriod) { $note = "$($Record.note); the driver declares no period below the default ($($Record.engineDefaultPeriodFrames) frames), nothing to judge" }
    }
    $label = if ($noSmallPeriod) { 'skipped (no small period)' } elseif ($passed) { 'ok' } elseif ($Record.required) { 'FAILED' } else { 'noted' }
    $reasons = @()
    if (-not $passed -and $Record.required -and $RoundRequired -and -not $noSmallPeriod) {
        $reasons += ('{0}: the tone arrived at {1} dB, expected {2} +- {3} dB ({4})' -f $Record.name, $Record.gainDb, $Record.expectGainDb, $Record.toleranceDb, $Record.note)
    }
    [pscustomobject]@{ Passed = $passed; Reasons = $reasons; SmallPeriodAvailable = $smallPeriodAvailable; Note = $note; Label = $label }
}

function Get-PluginLoadReuseVerdict($Record, [int] $SlowLoadMilliseconds) {
    $reasons = @()
    $opens = @($Record.opens)
    if ($opens.Count -lt 3) {
        return Get-CaptureGateVerdict @("plugin-load/fixture: only $($opens.Count) of the 3 capture opens ran")
    }
    foreach ($open in $opens) {
        if ($open.exitCode -ne 0 -or $null -eq $open.openMs) {
            return Get-CaptureGateVerdict @("plugin-load/fixture/$($open.name): the capture open failed (exit $($open.exitCode))")
        }
    }
    $loadFloorMs = 0.8 * $SlowLoadMilliseconds
    # Opens 2 and 3 test reuse only if the pipeline was really torn down in
    # between (GraphSettleSeconds + 2 s). On windows-2022 it was: before the
    # fix every open paid the full 4 s again.
    $reuseCeilingMs = 0.25 * $SlowLoadMilliseconds
    if ([double]$opens[0].openMs -lt $loadFloorMs) {
        $reasons += "plugin-load/fixture/open1: opened in $($opens[0].openMs) ms, under the fixture's $SlowLoadMilliseconds ms load, so the audio engine did not load the plugin"
    }
    foreach ($open in $opens[1..2]) {
        if ([double]$open.openMs -gt $reuseCeilingMs) {
            $reasons += "plugin-load/fixture/$($open.name): opened in $($open.openMs) ms; a rebuilt pipeline must reuse the plugin the engine kept loaded (limit $reuseCeilingMs ms)"
        }
    }
    Get-CaptureGateVerdict $reasons
}

function Get-PanelMeasurementVerdict {
    param(
        [ValidateSet('editor-pixels', 'panel-restore', 'no-unsaved-prompt', 'editor-close-timeout',
            'benchmark-timeout', 'editor-exit', 'benchmark-exit', 'background-state',
            'background-sound', 'edited-sound', 'asan')]
        [string] $Kind,
        $Observed,
        $Limit = 0
    )
    $passed = switch ($Kind) {
        'editor-pixels' { $Observed -ge $Limit }
        'panel-restore' { $Observed -lt $Limit }
        'no-unsaved-prompt' { -not $Observed }
        'editor-close-timeout' { [bool]$Observed }
        'benchmark-timeout' { [bool]$Observed }
        'edited-sound' { $Observed -ge $Limit }
        default { $Observed -eq 0 }
    }
    $reasons = @()
    if (-not $passed) { $reasons += "$Kind : $Observed" }
    [pscustomobject]@{ Passed = $passed; Reasons = $reasons }
}

# A panel capture is usable once the plug-in has painted (more colours than a
# blank window has) and two captures in a row are equal. The two conditions
# are judged apart so the gate can say which one a panel never met.
function Get-PanelSettleVerdict {
    param([int] $DistinctColors, [int] $MinimumColors, [bool] $EqualToPrevious)
    $painted = $DistinctColors -ge $MinimumColors
    $reason = if (-not $painted) { "not painted yet ($DistinctColors colour(s), a painted panel has at least $MinimumColors)" }
        elseif (-not $EqualToPrevious) { 'still changing' }
        else { 'painted and still' }
    [pscustomobject]@{ Painted = $painted; Settled = ($painted -and $EqualToPrevious); Reason = $reason }
}

function Get-PanelStateVerdict($Case, $State1, $State2) {
    $checks = @()
    $changed = @($State1.Keys | Where-Object { $State1[$_] -ne $State2[$_] })
    if ($Case.Format -eq 'VST3') {
        $checks += [pscustomobject]@{ check = 'vst3-panel-edit-saved'; expected = 'S2 differs from S1'; observed = $(if ($changed.Count -eq 0) { 'panel edit was not saved' } else { $changed -join ', ' }); pass = $changed.Count -gt 0 }
    }
    $checks += [pscustomobject]@{ check = 'changed-parameters'; expected = ">= $($Case.Expect.MinChangedParameters)"; observed = $changed -join ', '; pass = $changed.Count -ge $Case.Expect.MinChangedParameters }
    foreach ($key in $Case.Expect.Directions.Keys) {
        $delta = $State2[$key] - $State1[$key]
        $direction = $Case.Expect.Directions[$key]
        $pass = if ($direction -eq 'rises') { $delta -gt 0 } else { $delta -lt 0 }
        $checks += [pscustomobject]@{ check = "parameter-$key"; expected = $direction; observed = "$($State1[$key]) -> $($State2[$key])"; pass = $pass }
    }
    if ($Case.Expect.OthersUnchanged) {
        $unexpected = @($changed | Where-Object { $Case.Expect.Directions.Keys -notcontains $_ })
        $checks += [pscustomobject]@{ check = 'other-parameters-unchanged'; expected = 'only Low and High differ'; observed = $unexpected -join ', '; pass = $unexpected.Count -eq 0 }
    }
    $reasons = @($checks | Where-Object { -not $_.pass } | ForEach-Object { "$($_.check) : $($_.observed)" })
    [pscustomobject]@{ Passed = $reasons.Count -eq 0; Reasons = $reasons; Checks = $checks }
}

function Get-PanelGateVerdict($Checks) {
    $reasons = @($Checks | Where-Object { -not $_.pass } | ForEach-Object { "$($_.case) / $($_.check) : $($_.observed)" })
    [pscustomobject]@{ Passed = $reasons.Count -eq 0; Reasons = $reasons }
}

Export-ModuleMember -Function Get-CaptureGateVerdict, Get-GateFileVerdict, Get-CaptureMeasurementVerdict,
    Get-PluginLoadReuseVerdict, Get-PanelMeasurementVerdict, Get-PanelSettleVerdict, Get-PanelStateVerdict, Get-PanelGateVerdict
