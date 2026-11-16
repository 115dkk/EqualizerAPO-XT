Set-StrictMode -Version Latest

function Invoke-TestExecutable {
    <#
    .SYNOPSIS
        Runs one test executable with a deadline while streaming its output.

    .DESCRIPTION
        Start-Process redirects output to files, so this function polls those
        files and writes each new line to the CI log while the child is alive.
        The last stdout line is retained for the timeout diagnostic; the test
        harness flushes its case announcement before entering each case.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $FilePath,
        [string[]] $Arguments = @(),
        [Parameter(Mandatory)] [ValidateRange(0.1, 86400.0)] [double] $TimeoutSeconds,
        [string] $WorkingDirectory
    )

    $stdoutPath = [System.IO.Path]::GetTempFileName()
    $stderrPath = [System.IO.Path]::GetTempFileName()
    $stdoutCount = 0
    $stderrCount = 0
    $lastStdoutLine = $null
    $lastStderrLine = $null

    function Write-NewLines {
        param(
            [Parameter(Mandatory)] [string] $Path,
            [Parameter(Mandatory)] [ref] $LineCount,
            [Parameter(Mandatory)] [ref] $LastLine
        )

        $lines = @(Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue)
        for ($i = $LineCount.Value; $i -lt $lines.Count; $i++) {
            $line = "$($lines[$i])"
            Write-Host $line
            if (-not [string]::IsNullOrWhiteSpace($line)) { $LastLine.Value = $line }
        }
        $LineCount.Value = $lines.Count
    }

    $quotedArguments = @($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
    })
    $startArguments = @{
        FilePath = $FilePath
        ArgumentList = $quotedArguments
        PassThru = $true
        NoNewWindow = $true
        RedirectStandardOutput = $stdoutPath
        RedirectStandardError = $stderrPath
    }
    if ($WorkingDirectory) { $startArguments.WorkingDirectory = $WorkingDirectory }

    try {
        $process = Start-Process @startArguments
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        while (-not $process.HasExited -and $stopwatch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
            Write-NewLines -Path $stdoutPath -LineCount ([ref] $stdoutCount) -LastLine ([ref] $lastStdoutLine)
            Write-NewLines -Path $stderrPath -LineCount ([ref] $stderrCount) -LastLine ([ref] $lastStderrLine)
            Start-Sleep -Milliseconds 100
        }

        $timedOut = -not $process.HasExited
        if ($timedOut) {
            try { $process.Kill($true) } catch { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
        }
        $process.WaitForExit()
        Write-NewLines -Path $stdoutPath -LineCount ([ref] $stdoutCount) -LastLine ([ref] $lastStdoutLine)
        Write-NewLines -Path $stderrPath -LineCount ([ref] $stderrCount) -LastLine ([ref] $lastStderrLine)

        if ($timedOut) {
            $lastLine = if ($lastStdoutLine) { $lastStdoutLine } elseif ($lastStderrLine) { $lastStderrLine } else { "<no output>" }
            throw "$Name exceeded its $TimeoutSeconds-second deadline. Last output: $lastLine"
        }
        if ($process.ExitCode -ne 0) { throw "$Name exited with code $($process.ExitCode)" }
    } finally {
        Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    }
}

Export-ModuleMember -Function Invoke-TestExecutable
