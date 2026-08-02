# Pester 5 tests for .github/scripts/Bump-Version.ps1
#
# Bump-Version.ps1 is the release keystone: the CI version-bump job runs it on
# every main push to translate Conventional Commit messages into a version.h
# bump (and therefore into a release tag). A wrong decision here either skips a
# release that should have happened or cuts a release that should not have, so
# the SemVer mapping in CLAUDE.md is locked down with the cases below.
#
# Each test builds a throwaway git repo with crafted commits and a fixture
# version.h, runs the *real* script as a child process (the same way the CI
# `run:` step invokes it), then asserts on the rewritten version.h and the
# script's exit code / output. Running it out-of-process keeps the script's
# `exit` statements from tearing down the Pester host.

BeforeAll {
    $script:ScriptPath = (Resolve-Path (Join-Path $PSScriptRoot '..' 'Bump-Version.ps1')).Path
    # Use the same PowerShell executable that is hosting Pester so the child
    # process matches the edition under test (pwsh on CI).
    $script:PwshExe = (Get-Process -Id $PID).Path
    $script:TempRepos = [System.Collections.Generic.List[string]]::new()

    function New-FixtureRepo {
        $repo = Join-Path ([System.IO.Path]::GetTempPath()) ("bumpver-" + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $repo | Out-Null
        $script:TempRepos.Add($repo)
        & git -C $repo init -q | Out-Null
        & git -C $repo config user.email "bump-test@example.com" | Out-Null
        & git -C $repo config user.name  "Bump Test" | Out-Null
        # Hermetic: never depend on the runner's signing config for fixture commits.
        & git -C $repo config commit.gpgsign false | Out-Null
        & git -C $repo config tag.gpgsign false | Out-Null
        return $repo
    }

    function Set-FixtureVersion {
        param([string]$Repo, [int]$Major, [int]$Minor, [int]$Revision)
        $lines = @("#define MAJOR $Major", "#define MINOR $Minor", "#define REVISION $Revision")
        Set-Content -Path (Join-Path $Repo 'version.h') -Value $lines -Encoding ASCII
    }

    function Add-FixtureCommit {
        param([string]$Repo, [string]$Subject, [string]$Body)
        # --allow-empty lets a test craft any commit message without needing a
        # file change; Get-BumpKind only reads the messages.
        if ($PSBoundParameters.ContainsKey('Body')) {
            & git -C $repo commit -q --allow-empty -m $Subject -m $Body | Out-Null
        } else {
            & git -C $Repo commit -q --allow-empty -m $Subject | Out-Null
        }
    }

    function Add-FixtureTag {
        param([string]$Repo, [string]$Tag)
        & git -C $Repo tag $Tag | Out-Null
    }

    function Invoke-Bump {
        param([string]$Repo, [switch]$Check)
        $bumpArgs = @('-NoProfile', '-NonInteractive', '-File', $script:ScriptPath)
        if ($Check) { $bumpArgs += '-Check' }
        Push-Location $Repo
        try {
            $out = & $script:PwshExe @bumpArgs 2>&1 | Out-String
            $code = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        return [pscustomobject]@{ ExitCode = $code; Output = $out }
    }

    function Get-FixtureVersion {
        param([string]$Repo)
        $lines = Get-Content -Path (Join-Path $Repo 'version.h')
        $get = {
            param($name)
            foreach ($l in $lines) { if ($l -match "^\s*#define\s+$name\s+(\d+)\s*$") { return [int]$Matches[1] } }
            throw "missing $name"
        }
        return ("{0}.{1}.{2}" -f (& $get 'MAJOR'), (& $get 'MINOR'), (& $get 'REVISION'))
    }
}

AfterAll {
    foreach ($repo in $script:TempRepos) {
        Remove-Item -Path $repo -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Describe "Bump-Version.ps1" {
    Context "Conventional Commit -> SemVer bump kind" {
        It "bumps the minor for feat:" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "feat: add a knob"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "1.3.0"
        }

        It "bumps the revision for fix:" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "fix: correct a latch"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "1.2.4"
        }

        It "bumps the major for a feat!: breaking marker" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "feat!: change the config format"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "2.0.0"
        }

        It "treats the ! marker as breaking on any type, not just feat" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "refactor!: drop a removed option"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "2.0.0"
        }

        It "bumps the major for a BREAKING CHANGE footer in the body" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "fix: tweak parsing" -Body "BREAKING CHANGE: the old syntax is gone"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "2.0.0"
        }

        It "does not bump major when 'BREAKING CHANGE' only appears in prose, not as a footer" {
            # Regression: a ci: commit whose body merely discusses the rule (no
            # real footer, no type-bang marker) once matched the bare substring and
            # cut a spurious 2.0.0 release. It must stay a no-bump.
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "ci: add tests for the version bump rule" -Body "Coverage documents that a type-bang marker or a BREAKING CHANGE footer maps to a major bump."
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "1.2.3"
        }

        It "bumps the major for a BREAKING-CHANGE footer (hyphen spelling)" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "fix: tweak parsing" -Body "BREAKING-CHANGE: the old syntax is gone"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "2.0.0"
        }

        It "does not bump for non-release types (chore/docs/refactor)" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "chore: tidy"
            Add-FixtureCommit -Repo $repo -Subject "docs: clarify readme"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            $r.Output | Should -Match "No version-affecting commits"
            Get-FixtureVersion -Repo $repo | Should -Be "1.2.3"
        }

        It "prefers minor (feat) over patch (fix) when both are present" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "fix: a small bug"
            Add-FixtureCommit -Repo $repo -Subject "feat: a new thing"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "1.3.0"
        }

        It "prefers major over feat and fix when a breaking commit is present" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "feat: a new thing"
            Add-FixtureCommit -Repo $repo -Subject "fix: a small bug"
            Add-FixtureCommit -Repo $repo -Subject "perf!: drop the slow path"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "2.0.0"
        }

        It "recognises a scoped type like feat(scope):" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "feat(editor): add a panel"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "1.3.0"
        }

        It "ignores a conventional keyword that is not at the start of a line" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "chore: note the feat: keyword inline"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "1.2.3"
        }
    }

    Context "commit range is scoped to the last release tag" {
        It "ignores commits made before the last release tag" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 0 -Revision 0
            Add-FixtureCommit -Repo $repo -Subject "feat: shipped already"
            Add-FixtureTag -Repo $repo -Tag "v1.0.0"
            Add-FixtureCommit -Repo $repo -Subject "chore: housekeeping after release"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            $r.Output | Should -Match "No version-affecting commits"
            Get-FixtureVersion -Repo $repo | Should -Be "1.0.0"
        }

        It "bumps from the header counting only post-tag commits" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "feat: this one was released"
            Add-FixtureTag -Repo $repo -Tag "v1.2.3"
            Add-FixtureCommit -Repo $repo -Subject "fix: only this counts now"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Be 0
            Get-FixtureVersion -Repo $repo | Should -Be "1.2.4"
        }
    }

    Context "-Check reports without modifying version.h" {
        It "reports the would-be version and leaves the header untouched" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "feat: add a knob"
            $r = Invoke-Bump -Repo $repo -Check
            $r.ExitCode | Should -Be 0
            $r.Output | Should -Match "Next minor version would be 1.3.0"
            Get-FixtureVersion -Repo $repo | Should -Be "1.2.3"
        }

        It "reports no bump and leaves the header untouched" {
            $repo = New-FixtureRepo
            Set-FixtureVersion -Repo $repo -Major 1 -Minor 2 -Revision 3
            Add-FixtureCommit -Repo $repo -Subject "chore: tidy"
            $r = Invoke-Bump -Repo $repo -Check
            $r.ExitCode | Should -Be 0
            $r.Output | Should -Match "version stays at 1.2.3"
            Get-FixtureVersion -Repo $repo | Should -Be "1.2.3"
        }
    }

    Context "error handling" {
        It "fails when version.h is missing" {
            $repo = New-FixtureRepo
            Add-FixtureCommit -Repo $repo -Subject "feat: something"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Not -Be 0
            $r.Output | Should -Match "Version header not found"
        }

        It "fails when a version part is missing from the header" {
            $repo = New-FixtureRepo
            # MAJOR and MINOR only; REVISION is absent.
            Set-Content -Path (Join-Path $repo 'version.h') -Value @("#define MAJOR 1", "#define MINOR 2") -Encoding ASCII
            Add-FixtureCommit -Repo $repo -Subject "feat: something"
            $r = Invoke-Bump -Repo $repo
            $r.ExitCode | Should -Not -Be 0
            # Audit #250 F069: the message comes from the unified parser in
            # Get-VersionPart.ps1 now.
            $r.Output | Should -Match "Could not find version part: REVISION"
        }
    }
}
