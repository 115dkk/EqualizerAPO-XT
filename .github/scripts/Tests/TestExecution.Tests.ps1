Describe "TestExecution" {
    BeforeAll {
        Import-Module (Join-Path $PSScriptRoot "..\TestExecution.psm1") -Force
        $script:pwsh = (Get-Process -Id $PID).Path
    }

    It "kills a hung child and names its last output line" {
        {
            Invoke-TestExecutable -Name "HangingSuite" -FilePath $script:pwsh -Arguments @(
                "-NoProfile", "-Command",
                "[Console]::Out.WriteLine('SyntheticSuite::hangs'); [Console]::Out.Flush(); Start-Sleep -Seconds 30"
            ) -TimeoutSeconds 1
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
