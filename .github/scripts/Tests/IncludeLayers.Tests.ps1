# Audit #467 TD-39: these folder pairs included each other, so lower folders must not include higher ones.

BeforeAll {
    $script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
    $script:Rules = @(
        @{
            Folder = 'platform'
            Forbidden = @('services', 'devices', 'asio', 'engine', 'filters')
        },
        @{
            Folder = 'asio'
            Forbidden = @('devices')
        },
        @{
            Folder = 'runtime'
            Forbidden = @('asio', 'services', 'devices')
        }
    )
    # AlignedMemory's logging dependency predates TD-39 and is not one of the
    # three cycles moved here. The task says to report another violation rather
    # than change it, so keep that exact include as the sole baseline exception.
    $script:KnownOutOfScope = @('runtime/memory/AlignedMemory.cpp|services')
}

Describe 'folder include layers' {
    It 'keeps lower folders from including higher folders' {
        $offenses = [System.Collections.Generic.List[string]]::new()
        foreach ($rule in $script:Rules) {
            $folder = Join-Path $script:RepoRoot $rule.Folder
            $files = Get-ChildItem -LiteralPath $folder -Recurse -File |
                Where-Object { $_.Extension -in '.h', '.cpp' } |
                Sort-Object FullName
            foreach ($file in $files) {
                $lineNumber = 0
                foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
                    $lineNumber++
                    $match = [regex]::Match($line, '^\s*#\s*include\s*[<"](?<folder>[^/\\>"]+)[/\\]')
                    if ($match.Success -and $rule.Forbidden -contains $match.Groups['folder'].Value) {
                        $relative = [System.IO.Path]::GetRelativePath($script:RepoRoot, $file.FullName).Replace('\', '/')
                        $key = "$relative|$($match.Groups['folder'].Value)"
                        if ($script:KnownOutOfScope -notcontains $key) {
                            $offenses.Add("${relative}:$lineNumber`: $($line.Trim())")
                        }
                    }
                }
            }
        }

        if ($offenses.Count -ne 0) {
            throw "Audit #467 TD-39 include-layer violations:`n$($offenses -join "`n")"
        }
    }
}
