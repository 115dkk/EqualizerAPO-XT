# Pester 5 tests for .github/scripts/Update-Changelog.ps1
#
# The CI version-bump job runs this right after Bump-Version.ps1 to move the
# accumulated "## Unreleased" changelog entries under the version being cut. A
# wrong transform here either loses a release's user-facing notes or reflows
# the whole file, so the cases below lock the behaviour: the move, the
# empty/no-op case, the malformed-file error, and byte-level preservation
# (CRLF newlines, UTF-8 without BOM, em-dash separator).
#
# Each test writes a fixture changelog to a temp file, runs the *real* script
# as a child process (the same way the CI `run:` step invokes it), then asserts
# on the rewritten bytes. Out-of-process keeps the script's `throw` from
# tearing down the Pester host.

BeforeAll {
    $script:ScriptPath = (Resolve-Path (Join-Path $PSScriptRoot '..' 'Update-Changelog.ps1')).Path
    $script:PwshExe = (Get-Process -Id $PID).Path
    $script:EmDash = [char]0x2014
    $script:TempFiles = [System.Collections.Generic.List[string]]::new()

    function New-FixtureChangelog {
        param([string]$Text, [switch]$Bom)
        $path = Join-Path ([System.IO.Path]::GetTempPath()) ("changelog-" + [guid]::NewGuid().ToString('N') + ".md")
        $script:TempFiles.Add($path)
        $encoding = New-Object System.Text.UTF8Encoding($Bom.IsPresent)
        [System.IO.File]::WriteAllText($path, $Text, $encoding)
        return $path
    }

    function Invoke-UpdateChangelog {
        param([string]$Path, [string]$Version, [string]$Date)
        $updateArgs = @(
            '-NoProfile', '-NonInteractive', '-File', $script:ScriptPath,
            '-Version', $Version, '-Date', $Date, '-Paths', $Path
        )
        $output = & $script:PwshExe @updateArgs 2>&1
        return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output -join "`n") }
    }

    function Get-Bytes {
        param([string]$Path)
        return [System.IO.File]::ReadAllBytes($Path)
    }

    function Get-Text {
        param([string]$Path)
        return [System.IO.File]::ReadAllText($Path)
    }
}

AfterAll {
    foreach ($file in $script:TempFiles) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
    }
}

Describe "Update-Changelog.ps1" {
    It "files a non-empty Unreleased body under a dated version heading" {
        $crlf = "`r`n"
        $text = "# Changelog${crlf}${crlf}## Unreleased${crlf}${crlf}- entry A first line${crlf}  entry A second line (#5)${crlf}- entry B (#6)${crlf}${crlf}## v1.0.0 $EmDash 2026-01-01${crlf}${crlf}- old entry${crlf}"
        $path = New-FixtureChangelog -Text $text
        $result = Invoke-UpdateChangelog -Path $path -Version "1.1.0" -Date "2026-07-17"

        $result.ExitCode | Should -Be 0
        $out = Get-Text -Path $path
        $out | Should -Match ([regex]::Escape("## v1.1.0 $EmDash 2026-07-17"))
        # Unreleased is emptied and the new heading sits directly under it.
        $out | Should -Match ([regex]::Escape("## Unreleased${crlf}${crlf}## v1.1.0"))
        # The moved entries keep their exact text, including the wrapped line.
        $out | Should -Match ([regex]::Escape("- entry A first line${crlf}  entry A second line (#5)${crlf}- entry B (#6)"))
        # The previous section is untouched.
        $out | Should -Match ([regex]::Escape("## v1.0.0 $EmDash 2026-01-01${crlf}${crlf}- old entry"))
    }

    It "leaves an empty Unreleased section unchanged (no empty version section)" {
        $crlf = "`r`n"
        $text = "# Changelog${crlf}${crlf}## Unreleased${crlf}${crlf}## v1.0.0 $EmDash 2026-01-01${crlf}${crlf}- old entry${crlf}"
        $path = New-FixtureChangelog -Text $text
        $before = Get-Bytes -Path $path
        $result = Invoke-UpdateChangelog -Path $path -Version "1.1.0" -Date "2026-07-17"

        $result.ExitCode | Should -Be 0
        (Get-Bytes -Path $path) | Should -Be $before
    }

    It "fails loudly when there is no Unreleased section" {
        $crlf = "`r`n"
        $text = "# Changelog${crlf}${crlf}## v1.0.0 $EmDash 2026-01-01${crlf}${crlf}- old entry${crlf}"
        $path = New-FixtureChangelog -Text $text
        $result = Invoke-UpdateChangelog -Path $path -Version "1.1.0" -Date "2026-07-17"

        $result.ExitCode | Should -Not -Be 0
        $result.Output | Should -Match "Unreleased"
    }

    It "preserves CRLF newlines and does not introduce lone LFs" {
        $crlf = "`r`n"
        $text = "# Changelog${crlf}${crlf}## Unreleased${crlf}${crlf}- entry (#7)${crlf}${crlf}## v1.0.0 $EmDash 2026-01-01${crlf}${crlf}- old${crlf}"
        $path = New-FixtureChangelog -Text $text
        Invoke-UpdateChangelog -Path $path -Version "1.1.0" -Date "2026-07-17" | Out-Null

        $out = Get-Text -Path $path
        ([regex]::Matches($out, "(?<!`r)`n")).Count | Should -Be 0
        $out | Should -Match "`r`n"
    }

    It "preserves LF-only newlines when the file has no CRLF" {
        $lf = "`n"
        $text = "# Changelog${lf}${lf}## Unreleased${lf}${lf}- entry (#8)${lf}${lf}## v1.0.0 $EmDash 2026-01-01${lf}${lf}- old${lf}"
        $path = New-FixtureChangelog -Text $text
        Invoke-UpdateChangelog -Path $path -Version "1.1.0" -Date "2026-07-17" | Out-Null

        $out = Get-Text -Path $path
        $out | Should -Not -Match "`r`n"
        $out | Should -Match ([regex]::Escape("## v1.1.0 $EmDash 2026-07-17"))
    }

    It "does not add a UTF-8 BOM to a file that has none" {
        $crlf = "`r`n"
        $text = "# Changelog${crlf}${crlf}## Unreleased${crlf}${crlf}- entry (#9)${crlf}${crlf}## v1.0.0 $EmDash 2026-01-01${crlf}"
        $path = New-FixtureChangelog -Text $text
        Invoke-UpdateChangelog -Path $path -Version "1.1.0" -Date "2026-07-17" | Out-Null

        $bytes = Get-Bytes -Path $path
        ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) | Should -BeFalse
    }
}
