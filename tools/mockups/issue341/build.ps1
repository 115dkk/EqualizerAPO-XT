[CmdletBinding()]
param([switch]$Render, [switch]$Test, [switch]$Study)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$build = Join-Path $repo 'artifacts/issue341-build'
$output = Join-Path $repo 'docs/design/issue-341/images'
$qtBin = Join-Path $repo 'Qt/6.10.1/msvc2022_64/bin'
. (Join-Path $repo '.github/scripts/Import-VsDevEnvironment.ps1')
Import-VsDevEnvironment 'x64'
$env:PATH = "$qtBin;$env:PATH"
New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $build
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $PSScriptRoot 'issue341.pro') 'CONFIG+=release'
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & nmake /NOLOGO
    if ($LASTEXITCODE -ne 0) { throw 'nmake failed' }
    if ($Render -or $Test) {
        $oldPlatform = $env:QT_QPA_PLATFORM
        $env:QT_QPA_PLATFORM = 'offscreen'
        try {
            if ($Render) {
                $renderFlag = if ($Study) { '--render-study' } else { '--render' }
                $renderOutput = if ($Study) { Join-Path $output 'integrated' } else { $output }
                & "$build/release/Issue341Mockup.exe" --repo $repo $renderFlag $renderOutput
                if ($LASTEXITCODE -ne 0) { throw 'Render failed' }
            }
            if ($Test) {
                $testFlag = if ($Study) { '--test-study' } else { '--test' }
                if ($Study) {
                    $qaFolder = Join-Path $output 'integrated'
                    New-Item -ItemType Directory -Force -Path $qaFolder | Out-Null
                    & "$build/release/Issue341Mockup.exe" --repo $repo $testFlag --report (Join-Path $qaFolder 'qa.json')
                } else { & "$build/release/Issue341Mockup.exe" --repo $repo $testFlag }
                if ($LASTEXITCODE -ne 0) { throw 'Mockup tests failed' }
            }
        } finally { $env:QT_QPA_PLATFORM = $oldPlatform }
    }
} finally { Pop-Location }
