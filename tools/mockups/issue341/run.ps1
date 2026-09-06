[CmdletBinding()]
param([ValidateSet('studio-dark','studio-light','soft-dark','soft-light')][string]$Theme = 'soft-light', [switch]$Study)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$exe = Join-Path $repo 'artifacts/issue341-build/release/Issue341Mockup.exe'
if (-not (Test-Path -LiteralPath $exe)) { & (Join-Path $PSScriptRoot 'build.ps1') }
$env:PATH = (Join-Path $repo 'Qt/6.10.1/msvc2022_64/bin') + ';' + $env:PATH
if ($Study) { & $exe --repo $repo --theme $Theme --study }
else { & $exe --repo $repo --theme $Theme }
if ($LASTEXITCODE -ne 0) { throw "Mockup exited with code $LASTEXITCODE" }
