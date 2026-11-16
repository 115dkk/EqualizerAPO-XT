Describe "TestExecution" {
    BeforeAll {
        Import-Module (Join-Path $PSScriptRoot "..\TestExecution.psm1") -Force
        $script:pwsh = (Get-Process -Id $PID).Path
    }

    It "kills a hung child and names its last output line" {
        # The deadline has to outlast the child's own start-up: a pwsh that
        # takes more than a second to print its first line on a busy runner
        # would be killed before it said anything (seen locally under a
        # parallel build, audit #467 TD-29).
        {
            Invoke-TestExecutable -Name "HangingSuite" -FilePath $script:pwsh -Arguments @(
                "-NoProfile", "-Command",
                "[Console]::Out.WriteLine('SyntheticSuite::hangs'); [Console]::Out.Flush(); Start-Sleep -Seconds 30"
            ) -TimeoutSeconds 6
        } | Should -Throw "*SyntheticSuite::hangs*"
    }

    It "fails when a child exits non-zero" {
        {
            Invoke-TestExecutable -Name "FailingSuite" -FilePath $script:pwsh -Arguments @(
                "-NoProfile", "-Command", "exit 7"
            ) -TimeoutSeconds 10
        } | Should -Throw "*FailingSuite exited with code 7*"
    }
}
