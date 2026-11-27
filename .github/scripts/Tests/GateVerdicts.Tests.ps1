BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '../GateVerdicts.psm1') -Force
    function New-CaptureRecord {
        [pscustomobject]@{
            name = 'default/tone'; exitCode = 0; gainDb = -6.0
            expectGainDb = -6.0; toleranceDb = 1.5; required = $true
            period = ''; engineMinPeriodFrames = $null; engineDefaultPeriodFrames = $null
            note = 'expected tone'
        }
    }
    function New-LoadRecord {
        [pscustomobject]@{ opens = @(
            [pscustomobject]@{ name = 'open1'; exitCode = 0; openMs = 4000 }
            [pscustomobject]@{ name = 'open2'; exitCode = 0; openMs = 100 }
            [pscustomobject]@{ name = 'open3'; exitCode = 0; openMs = 100 }
        ) }
    }
}

Describe 'Row coverage of a list check' {
    It 'passes when exactly the expected skins drew rows' {
        (Get-RowCoverageVerdict -Line 'ListMode: rows in flat, matrix, minimal, rack, studio; cards in no skin' -ExpectedSkins @('flat', 'matrix', 'minimal', 'rack', 'studio')).Passed | Should -BeTrue
        (Get-RowCoverageVerdict -Line 'ListMode: rows in studio; cards in no skin' -ExpectedSkins @('studio')).Passed | Should -BeTrue
    }

    It 'fails when a skin fell back to cards' {
        $verdict = Get-RowCoverageVerdict -Line 'ListMode: rows in rack, studio; cards in flat, matrix, minimal' -ExpectedSkins @('flat', 'matrix', 'minimal', 'rack', 'studio')
        $verdict.Passed | Should -BeFalse
        $verdict.Reasons | Should -Be @('flat fell back to cards', 'matrix fell back to cards', 'minimal fell back to cards')
    }

    It 'fails when no skin drew rows, an expected skin did not run, or another skin ran' {
        (Get-RowCoverageVerdict -Line 'ListMode: rows in no skin; cards in studio' -ExpectedSkins @('studio')).Reasons | Should -Be @('studio fell back to cards')
        (Get-RowCoverageVerdict -Line 'ListMode: rows in rack; cards in no skin' -ExpectedSkins @('rack', 'studio')).Reasons | Should -Be @('studio did not run')
        (Get-RowCoverageVerdict -Line 'ListMode: rows in rack; cards in no skin' -ExpectedSkins @('studio')).Reasons | Should -Be @('studio did not run', 'rack drew rows but the gate does not name it')
    }

    It 'fails when the run printed no coverage line' {
        (Get-RowCoverageVerdict -Line '' -ExpectedSkins @('studio')).Passed | Should -BeFalse
        (Get-RowCoverageVerdict -Line 'CloseSaveTest: PASS' -ExpectedSkins @('studio')).Reasons | Should -Be @('the run did not report which skins drew rows')
    }
}

Describe 'Capture script wiring with a fake probe' {
    BeforeAll {
        $path = Join-Path $PSScriptRoot '../Invoke-CaptureGate.ps1'
        $tokens = $null; $errors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
        if ($errors.Count -gt 0) { throw $errors[0] }
        # Load only these functions, never the script's driver/service setup.
        foreach ($name in @('Measure-Cable', 'Get-JsonField', 'Add-Failure')) {
            $node = $ast.Find({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $true)
            . ([scriptblock]::Create($node.Extent.Text))
        }
        function Invoke-ExternalProgram {
            [pscustomobject]@{ ExitCode = $script:probeExit; StdOut = '{"gainDb":-6,"toneDb":-24,"rmsDb":-24,"silentPackets":0,"rate":48000}' }
        }
        $captureConnection = 'fake capture'; $renderConnection = 'fake render'; $captureProbe = 'not-executed.exe'
        $roundRequired = $true
    }
    It 'uses the extracted verdict for probe exit <Exit>' -ForEach @(
        @{ Exit = 0; FailureCount = 0; Passed = $true }
        @{ Exit = 2; FailureCount = 1; Passed = $false }
    ) {
        $script:probeExit = $Exit
        $summary = @{ measurements = @(); failures = @() }
        $measurement = [pscustomobject]@{ Name = 'tone'; Category = 'default'; Raw = $false; ExpectGainDb = -6.0; ToleranceDb = 1.5; Required = $true; Note = 'expected tone' }
        $record = Measure-Cable $measurement 'default'
        $record.passed | Should -Be $Passed
        $summary.measurements.Count | Should -Be 1
        $summary.failures.Count | Should -Be $FailureCount
    }
}

Describe 'Capture measurement verdict' {
    It 'accepts the probe success without repeating its level calculation' {
        $r = Get-CaptureMeasurementVerdict (New-CaptureRecord) $true
        $r.Passed | Should -BeTrue
        $r.Reasons.Count | Should -Be 0
        $r.Label | Should -Be 'ok'
    }
    It 'rejects <Name> reported by the gain probe' -ForEach @(
        @{ Name = 'silence instead of the tone'; Gain = -300.0; Expected = -6.0 }
        @{ Name = 'a tone instead of the attenuated signal'; Gain = 0.0; Expected = -90.0 }
        @{ Name = 'a level outside tolerance'; Gain = -3.0; Expected = -6.0 }
    ) {
        $record = New-CaptureRecord
        $record.exitCode = 2; $record.gainDb = $Gain; $record.expectGainDb = $Expected
        $r = Get-CaptureMeasurementVerdict $record $true
        $r.Passed | Should -BeFalse
        $r.Label | Should -Be 'FAILED'
        $r.Reasons.Count | Should -Be 1
        $r.Reasons[0] | Should -BeExactly ("default/tone: the tone arrived at {0} dB, expected {1} +- 1.5 dB (expected tone)" -f $Gain, $Expected)
    }
    It 'rejects a failed probe with no JSON measurement' {
        $record = New-CaptureRecord; $record.exitCode = 1; $record.gainDb = $null
        (Get-CaptureMeasurementVerdict $record $true).Reasons.Count | Should -Be 1
    }
    It 'records rather than fails an optional measurement' {
        $record = New-CaptureRecord; $record.exitCode = 2; $record.required = $false
        $r = Get-CaptureMeasurementVerdict $record $true
        $r.Passed | Should -BeFalse
        $r.Reasons.Count | Should -Be 0
        $r.Label | Should -Be 'noted'
    }
    It 'does not fail an optional installation round' {
        $record = New-CaptureRecord; $record.exitCode = 2
        $r = Get-CaptureMeasurementVerdict $record $false
        $r.Reasons.Count | Should -Be 0
        $r.Label | Should -Be 'FAILED'
    }
    It 'skips a minimum-period request when no smaller period exists' -ForEach @(
        @{ Minimum = 480 }; @{ Minimum = 960 }
    ) {
        $record = New-CaptureRecord; $record.exitCode = 2; $record.period = 'min'
        $record.engineMinPeriodFrames = $Minimum; $record.engineDefaultPeriodFrames = 480
        $r = Get-CaptureMeasurementVerdict $record $true
        $r.SmallPeriodAvailable | Should -BeFalse
        $r.Label | Should -Be 'skipped (no small period)'
        $r.Reasons.Count | Should -Be 0
        $r.Note | Should -BeExactly 'expected tone; the driver declares no period below the default (480 frames), nothing to judge'
    }
    It 'does not skip an available smaller period' {
        $record = New-CaptureRecord; $record.exitCode = 2; $record.period = 'min'
        $record.engineMinPeriodFrames = 128; $record.engineDefaultPeriodFrames = 480
        $r = Get-CaptureMeasurementVerdict $record $true
        $r.SmallPeriodAvailable | Should -BeTrue
        $r.Reasons.Count | Should -Be 1
    }
    It 'does not infer a missing period value as zero' -ForEach @(
        @{ Minimum = $null; Default = 480 }; @{ Minimum = 128; Default = $null }
    ) {
        $record = New-CaptureRecord; $record.exitCode = 2; $record.period = 'min'
        $record.engineMinPeriodFrames = $Minimum; $record.engineDefaultPeriodFrames = $Default
        $r = Get-CaptureMeasurementVerdict $record $true
        $r.SmallPeriodAvailable | Should -BeNullOrEmpty
        $r.Reasons.Count | Should -Be 1
    }
    It 'does not skip default-period measurements' {
        $record = New-CaptureRecord; $record.exitCode = 2; $record.period = 'default'
        $record.engineMinPeriodFrames = 480; $record.engineDefaultPeriodFrames = 480
        (Get-CaptureMeasurementVerdict $record $true).Reasons.Count | Should -Be 1
    }
}

Describe 'Capture plugin reuse verdict' {
    It 'accepts a slow first open and quick subsequent opens at the boundaries' {
        $record = New-LoadRecord; $record.opens[0].openMs = 3200
        $record.opens[1].openMs = 1000; $record.opens[2].openMs = 1000
        (Get-PluginLoadReuseVerdict $record 4000).Passed | Should -BeTrue
    }
    It 'rejects an incomplete sequence' {
        $record = New-LoadRecord; $record.opens = @($record.opens[0])
        $r = Get-PluginLoadReuseVerdict $record 4000
        $r.Passed | Should -BeFalse
        $r.Reasons | Should -Contain 'plugin-load/fixture: only 1 of the 3 capture opens ran'
    }
    It 'rejects a failed or missing open result' -ForEach @(
        @{ Exit = 1; Time = 4000 }; @{ Exit = 0; Time = $null }
    ) {
        $record = New-LoadRecord; $record.opens[0].exitCode = $Exit; $record.opens[0].openMs = $Time
        $r = Get-PluginLoadReuseVerdict $record 4000
        $r.Passed | Should -BeFalse
        $r.Reasons.Count | Should -Be 1
        $r.Reasons[0] | Should -Match 'capture open failed'
    }
    It 'rejects an initial open which never paid for the fixture load' {
        $record = New-LoadRecord; $record.opens[0].openMs = 3199
        (Get-PluginLoadReuseVerdict $record 4000).Reasons[0] | Should -Match 'audio engine did not load the plugin'
    }
    It 'rejects each rebuilt pipeline paying the load cost again' -ForEach @(1, 2) {
        $record = New-LoadRecord; $record.opens[$_].openMs = 1001
        $r = Get-PluginLoadReuseVerdict $record 4000
        $r.Passed | Should -BeFalse
        $r.Reasons[0] | Should -Match 'must reuse the plugin'
    }
}

Describe 'Missing input files and final gate verdicts' {
    It 'accepts an existing probe file' {
        (Get-GateFileVerdict $true 'probe not found: fixture.exe').Passed | Should -BeTrue
    }
    It 'preserves the missing-file reason' {
        $r = Get-GateFileVerdict $false 'probe not found: fixture.exe'
        $r.Passed | Should -BeFalse
        $r.Reasons | Should -Contain 'probe not found: fixture.exe'
    }
    It 'accepts no capture failures and rejects every reported failure' {
        (Get-CaptureGateVerdict @()).Passed | Should -BeTrue
        $r = Get-CaptureGateVerdict @('install failed', 'tone missing')
        $r.Passed | Should -BeFalse
        ($r.Reasons -join '|') | Should -Be 'install failed|tone missing'
    }
    It 'accepts passed panel checks and preserves each failed check reason' {
        $checks = @([pscustomobject]@{ case = 'eq'; check = 'editor-pixels'; observed = 100; pass = $true })
        (Get-PanelGateVerdict $checks).Passed | Should -BeTrue
        $checks += [pscustomobject]@{ case = 'eq'; check = 'edited-sound'; observed = 0; pass = $false }
        $r = Get-PanelGateVerdict $checks
        $r.Passed | Should -BeFalse
        $r.Reasons.Count | Should -Be 1
        $r.Reasons[0] | Should -Be 'eq / edited-sound : 0'
    }
}

Describe 'Panel measurement verdicts' {
    It 'judges <Kind> at its passing and failing boundaries' -ForEach @(
        @{ Kind = 'editor-pixels'; Good = 100; Bad = 99; Limit = 100 }
        @{ Kind = 'panel-restore'; Good = 9; Bad = 10; Limit = 10 }
        @{ Kind = 'no-unsaved-prompt'; Good = $false; Bad = $true; Limit = 0 }
        @{ Kind = 'editor-close-timeout'; Good = $true; Bad = $false; Limit = 0 }
        @{ Kind = 'benchmark-timeout'; Good = $true; Bad = $false; Limit = 0 }
        @{ Kind = 'editor-exit'; Good = 0; Bad = 1; Limit = 0 }
        @{ Kind = 'benchmark-exit'; Good = 0; Bad = 1; Limit = 0 }
        @{ Kind = 'background-state'; Good = 0; Bad = 1; Limit = 0 }
        @{ Kind = 'background-sound'; Good = 0; Bad = 1; Limit = 0 }
        @{ Kind = 'edited-sound'; Good = 33; Bad = 32; Limit = 33 }
        @{ Kind = 'asan'; Good = 0; Bad = 1; Limit = 0 }
    ) {
        (Get-PanelMeasurementVerdict $Kind $Good $Limit).Passed | Should -BeTrue
        $r = Get-PanelMeasurementVerdict $Kind $Bad $Limit
        $r.Passed | Should -BeFalse
        $r.Reasons.Count | Should -Be 1
    }
    It 'rejects misspelled measurement kinds rather than silently passing' {
        { Get-PanelMeasurementVerdict 'edit-sound' 0 33 } | Should -Throw
    }
}

Describe 'Panel settle verdict' {
    # An unpainted plug-in window is one flat colour and equal to itself. The
    # gate used to take two equal captures for a settled panel, compared that
    # blank picture with the edited one, and failed panel-restore on the ASan
    # leg (main after #468, PR #470).
    It 'does not take two equal captures of an unpainted window for a settled panel' {
        $r = Get-PanelSettleVerdict -DistinctColors 1 -MinimumColors 16 -EqualToPrevious $true
        $r.Painted | Should -BeFalse
        $r.Settled | Should -BeFalse
        $r.Reason | Should -Match 'not painted yet'
    }
    It 'waits for a painted panel that is still changing' {
        $r = Get-PanelSettleVerdict -DistinctColors 16 -MinimumColors 16 -EqualToPrevious $false
        $r.Painted | Should -BeTrue
        $r.Settled | Should -BeFalse
        $r.Reason | Should -Be 'still changing'
    }
    It 'accepts a painted panel whose two captures are equal' {
        $r = Get-PanelSettleVerdict -DistinctColors 16 -MinimumColors 16 -EqualToPrevious $true
        $r.Settled | Should -BeTrue
    }
    It 'counts the colours of a capture up to the limit' {
        # The C# helper needs the desktop assemblies the gate loads; compile it
        # the way the gate does and feed it synthetic pixels (no screen access).
        $source = Join-Path $PSScriptRoot '../PanelInputDesktop.cs'
        $counted = & powershell.exe -NoProfile -Command @"
Add-Type -Path '$source' -ReferencedAssemblies @('UIAutomationClient', 'UIAutomationTypes', 'WindowsBase', 'System.Drawing')
`$flat = New-Object byte[] 400
for (`$i = 0; `$i -lt 400; `$i += 4) { `$flat[`$i] = 7; `$flat[`$i + 1] = 7; `$flat[`$i + 2] = 7; `$flat[`$i + 3] = 255 }
`$many = New-Object byte[] 400
for (`$i = 0; `$i -lt 400; `$i += 4) { `$many[`$i] = [byte](`$i / 4); `$many[`$i + 3] = 255 }
'{0} {1}' -f [PanelInputGate.Desktop]::DistinctColors(`$flat, 16), [PanelInputGate.Desktop]::DistinctColors(`$many, 16)
"@
        $LASTEXITCODE | Should -Be 0
        ($counted | Select-Object -Last 1) | Should -Be '1 16'
    }
}

Describe 'Panel state verdict' {
    BeforeEach {
        $case = [pscustomobject]@{ Format = 'VST3'; Expect = [pscustomobject]@{
            MinChangedParameters = 2; Directions = [ordered]@{ Low = 'rises'; High = 'falls' }; OthersUnchanged = $true
        } }
        $before = @{ Low = 0.5; High = 0.5; Mid = 0.5 }
        $after = @{ Low = 0.6; High = 0.4; Mid = 0.5 }
    }
    It 'accepts both intended EQ changes' {
        $r = Get-PanelStateVerdict $case $before $after
        $r.Passed | Should -BeTrue
        $r.Checks.Count | Should -Be 5
    }
    It 'rejects an unsaved VST3 edit and too few changed parameters' {
        $r = Get-PanelStateVerdict $case $before $before
        $r.Passed | Should -BeFalse
        @($r.Checks | Where-Object { -not $_.pass }).check | Should -Contain 'vst3-panel-edit-saved'
        @($r.Checks | Where-Object { -not $_.pass }).check | Should -Contain 'changed-parameters'
    }
    It 'rejects each parameter moving in the wrong direction' -ForEach @('Low', 'High') {
        $after[$_] = 1.0 - $after[$_]
        $r = Get-PanelStateVerdict $case $before $after
        @($r.Checks | Where-Object { -not $_.pass }).check | Should -Contain "parameter-$_"
    }
    It 'rejects an unexpected EQ parameter change' {
        $after.Mid = 0.7
        $r = Get-PanelStateVerdict $case $before $after
        @($r.Checks | Where-Object { -not $_.pass }).check | Should -Contain 'other-parameters-unchanged'
    }
    It 'omits the VST3 check for VST2 and the direction checks for MVerb' {
        $case.Format = 'VST2'; $case.Expect.Directions = [ordered]@{}; $case.Expect.OthersUnchanged = $false
        $r = Get-PanelStateVerdict $case $before $after
        $r.Passed | Should -BeTrue
        $r.Checks.Count | Should -Be 1
        $r.Checks[0].check | Should -Be 'changed-parameters'
    }
}
