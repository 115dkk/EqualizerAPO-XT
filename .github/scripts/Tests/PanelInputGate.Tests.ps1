BeforeAll {
    $script:gate = Join-Path $PSScriptRoot '../Invoke-PanelInputGate.ps1'
    $script:plan = & $gate -WorkspaceRoot 'unused-plan-root' -PlanOnly
}

Describe 'Panel input gate plan' {
    It 'returns exactly one plan object without loading the desktop helper' {
        @($plan).Count | Should -Be 1
        ('PanelInputGate.Desktop' -as [type]) | Should -BeNullOrEmpty
    }

    It 'runs all four cases in the specified order' {
        ($plan.Cases.Name -join ',') | Should -Be 'eq-vst2,eq-vst3,mverb-vst2,mverb-vst3'
        ($plan.Cases.Format -join ',') | Should -Be 'VST2,VST3,VST2,VST3'
        ($plan.Cases.PluginKey -join ',') | Should -Be 'EQ_VST2,EQ_VST3,MVERB_VST2,MVERB_VST3'
        foreach ($case in $plan.Cases) { $case.WindowClassPrefix | Should -BeExactly 'DISTRHO-' }
    }

    It 'pins both edits and the negative-control drag' {
        foreach ($case in $plan.Cases) {
            $case.Edits.Count | Should -Be 2
            $paths = @(@($case.Edits) + @($case.Background) | ForEach-Object {
                '{0},{1}->{2},{3}' -f $_.From.X, $_.From.Y, $_.To.X, $_.To.Y
            })
            if ($case.Name.StartsWith('eq-')) {
                ($paths -join ';') | Should -Be '82,138->82,98;208,138->208,168;360,100->360,60'
            } else {
                ($paths -join ';') | Should -Be '312,56->312,26;184,56->184,86;20,56->20,86'
            }
        }
    }

    It 'keeps every point inside the reference plugin client area' {
        foreach ($case in $plan.Cases) {
            $width = 456; $height = 108
            if ($case.Name.StartsWith('eq-')) { $width = 392; $height = 372 }
            foreach ($drag in @($case.Edits) + @($case.Background)) {
                $drag.Steps | Should -Be 12
                foreach ($point in @($drag.From, $drag.To)) {
                    $point.X | Should -BeGreaterOrEqual 0
                    $point.X | Should -BeLessThan $width
                    $point.Y | Should -BeGreaterOrEqual 0
                    $point.Y | Should -BeLessThan $height
                }
            }
        }
    }

    It 'requires Low to rise, High to fall, and all other EQ parameters to stay unchanged' {
        foreach ($case in @($plan.Cases | Where-Object { $_.Name.StartsWith('eq-') })) {
            $case.Expect.Directions.Count | Should -Be 2
            $case.Expect.Directions.Low | Should -Be 'rises'
            $case.Expect.Directions.High | Should -Be 'falls'
            $case.Expect.OthersUnchanged | Should -BeTrue
            $case.Expect.MinChangedParameters | Should -Be 2
            $case.ParameterNames.Count | Should -Be 6
        }
    }

    It 'requires two MVerb changes without asserting their names' {
        foreach ($case in @($plan.Cases | Where-Object { $_.Name.StartsWith('mverb-') })) {
            $case.Expect.Directions.Count | Should -Be 0
            $case.Expect.OthersUnchanged | Should -BeFalse
            $case.Expect.MinChangedParameters | Should -Be 2
            $case.ParameterNames.Count | Should -Be 9
        }
    }

    It 'pins the complete parameter vocabulary for each format' {
        ($plan.Cases[0].ParameterNames -join ',') | Should -Be 'Low,Mid,High,Master,Low-Mid Freq,Mid-High Freq'
        ($plan.Cases[1].ParameterNames -join ',') | Should -Be 'low,mid,high,master,low_mid,mid_high'
        ($plan.Cases[2].ParameterNames -join ',') | Should -Be 'Damping,Density,Bandwidth,Decay,Predelay,Size,Gain,Mix,Early/Late Mix'
        ($plan.Cases[3].ParameterNames -join ',') | Should -Be 'damping,density,bandwidth,decay,predelay,size,gain,mix,earlymix'
    }

    It 'pins the deterministic three-second float input and the thresholds' {
        $plan.Signal.SampleRate | Should -Be 48000
        $plan.Signal.Channels | Should -Be 2
        # A whole number of Benchmark's 512-frame batches, about 3 s.
        $plan.Signal.Frames | Should -Be 147456
        ($plan.Signal.Frames % 512) | Should -Be 0
        $plan.Signal.Format | Should -Be 'float32'
        $plan.Signal.SweepStartHz | Should -Be 20
        $plan.Signal.SweepEndHz | Should -Be 20000
        $plan.Signal.SweepDbfs | Should -Be -18
        $plan.Signal.NoiseDbfs | Should -Be -30
        ($plan.Signal.Seeds -join ',') | Should -Be '12345678,9abcdef0'
        $plan.MinEditorPixelsChanged | Should -Be 100
        $plan.MinSoundChangeLsb | Should -Be 33
    }

    It 'pins Benchmark arguments including the per-render substitutions' {
        ($plan.BenchmarkArguments -join ' ') | Should -Be '--config-path {configDirectory} -i {inputWav} -o {outputWav} --batchsize 512 --nopause'
    }

    It 'pins timeouts and capture timing' {
        $plan.Timeouts.BenchmarkSeconds | Should -Be 120
        $plan.Timeouts.StartupSeconds | Should -Be 60
        $plan.Timeouts.StartupDialogsSeconds | Should -Be 10
        $plan.Timeouts.PanelSeconds | Should -Be 10
        $plan.Timeouts.ButtonSeconds | Should -Be 15
        $plan.Timeouts.SettleSeconds | Should -Be 5
        $plan.Timeouts.CaptureIntervalMilliseconds | Should -Be 300
        $plan.Timeouts.SaveSeconds | Should -Be 10
        $plan.Timeouts.ExitSeconds | Should -Be 30
    }

    It 'does not set ASan options for the shipped Editor' {
        $plan.AsanOptions | Should -BeNullOrEmpty
        ($null -eq $plan.AsanOptions) | Should -BeTrue
    }

    It 'sets the ASan log prefix and fail-fast option, quoting the drive colon' {
        $asanPlan = & $gate -WorkspaceRoot 'unused' -AsanLogDirectory 'C:\gate logs\asan\' -PlanOnly
        $asanPlan.AsanOptions | Should -BeExactly 'log_path="C:\gate logs\asan\asan":halt_on_error=1'
        $asanPlan.AsanOptions | Should -Match 'halt_on_error=1'
        $asanPlan.AsanOptions | Should -Match ([regex]::Escape('C:\gate logs\asan\asan'))
    }

    It 'narrows a manual run to eq-vst3 while retaining an array' {
        $one = & $gate -WorkspaceRoot 'unused' -Cases eq-vst3 -PlanOnly
        $one.Cases.Count | Should -Be 1
        $one.Cases[0].Name | Should -Be 'eq-vst3'
        $one.Cases[0].PluginKey | Should -Be 'EQ_VST3'
    }

    It 'preserves canonical order and does not run duplicates' {
        $two = & $gate -WorkspaceRoot 'unused' -Cases @('mverb-vst3', 'eq-vst2', 'eq-vst2') -PlanOnly
        ($two.Cases.Name -join ',') | Should -Be 'eq-vst2,mverb-vst3'
    }

    It 'rejects unknown case names before doing any work' {
        { & $gate -WorkspaceRoot 'unused' -Cases 'eq-vst9' -PlanOnly } | Should -Throw '*Unknown panel input case*'
    }

    It 'has no filesystem or environment side effects in PlanOnly' {
        $work = Join-Path $TestDrive 'not-created'
        $beforeAsan = $env:ASAN_OPTIONS
        $null = & $gate -WorkspaceRoot $work -AppDirectory $work -PluginManifest (Join-Path $work 'absent.json') -WorkDirectory $work -SnapshotDirectory $work -AsanLogDirectory $work -PlanOnly
        Test-Path -LiteralPath $work | Should -BeFalse
        $env:ASAN_OPTIONS | Should -Be $beforeAsan
    }
}
