Describe "Initialize-DepsEnvironment.ps1 planning" {
    BeforeAll {
        $scriptPath = Join-Path $PSScriptRoot "..\Initialize-DepsEnvironment.ps1"

        function NewDepsTree {
            $deps = Join-Path $TestDrive ([guid]::NewGuid().ToString("N"))
            # FFTW nests per archive layout, so the script probes for the files.
            New-Item -ItemType Directory -Force -Path "$deps\fftw\Include\nested" | Out-Null
            Set-Content "$deps\fftw\Include\nested\fftw3.h" "// header"
            New-Item -ItemType Directory -Force -Path "$deps\fftw\Release\x64" | Out-Null
            Set-Content "$deps\fftw\Release\x64\libfftw3.lib" "lib"
            New-Item -ItemType Directory -Force -Path "$deps\muparserx\parser" | Out-Null
            New-Item -ItemType Directory -Force -Path "$deps\muparserx\build\Release" | Out-Null
            Set-Content "$deps\muparserx\build\Release\muparserx.lib" "lib"
            $deps
        }
    }

    It "resolves the probed paths to the directories holding the files" {
        $deps = NewDepsTree
        $plan = & $scriptPath -DepsPath $deps -PlanOnly
        $plan.FFTW_INCLUDE | Should -Be (Join-Path $deps "fftw\Include\nested")
        $plan.FFTW_LIB | Should -Be (Join-Path $deps "fftw\Release\x64")
        $plan.MUPARSERX_LIB | Should -Be (Join-Path $deps "muparserx\build\Release")
    }

    It "spells the fixed roots off the deps path" {
        $deps = NewDepsTree
        $plan = & $scriptPath -DepsPath $deps -PlanOnly
        $plan.MUPARSERX_INCLUDE | Should -Be "$deps\muparserx\parser"
        $plan.LIBSNDFILE_INCLUDE | Should -Be "$deps\libsndfile\include"
        $plan.LIBSNDFILE_LIB | Should -Be "$deps\libsndfile\build\Release"
        $plan.TCLAP_ROOT | Should -Be "$deps\tclap"
        $plan.VST3_SDK | Should -Be "$deps\vst3sdk"
        $plan.HIGHWAY_INCLUDE | Should -Be "$deps\highway"
        $plan.ASIO_SDK | Should -Be "$deps\asiosdk\ASIOSDK"
        $plan.VELOPACK_INCLUDE | Should -Be "$deps\velopack_libc\include"
        $plan.VELOPACK_LIB | Should -Be "$deps\velopack_libc\lib"
    }

    It "fails loudly on each of the four fatal absences" {
        $deps = NewDepsTree
        Remove-Item "$deps\fftw\Include" -Recurse -Force
        { & $scriptPath -DepsPath $deps -PlanOnly } | Should -Throw "*fftw3.h not found*"

        $deps = NewDepsTree
        Remove-Item "$deps\fftw\Release" -Recurse -Force
        { & $scriptPath -DepsPath $deps -PlanOnly } | Should -Throw "*libfftw3.lib not found*"

        $deps = NewDepsTree
        Remove-Item "$deps\muparserx\build" -Recurse -Force
        { & $scriptPath -DepsPath $deps -PlanOnly } | Should -Throw "*muparserx.lib not found*"

        $deps = NewDepsTree
        Remove-Item "$deps\muparserx" -Recurse -Force
        { & $scriptPath -DepsPath $deps -PlanOnly } | Should -Throw "*muparserx folder not found*"
    }
}
