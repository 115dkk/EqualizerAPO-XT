<#
.SYNOPSIS
    Defines Import-VsDevEnvironment: locates Visual Studio via vswhere and
    imports the VsDevCmd.bat environment into the current PowerShell session.

.DESCRIPTION
    Dot-source this file, then call the function with the target architecture:

      . .\.github\scripts\Import-VsDevEnvironment.ps1
      Import-VsDevEnvironment "x64"   # or "arm64"

    Shared by .github/workflows/build.yml (the vcpkg dependency build and the
    Qt qmake/nmake build steps) and setup-build.ps1, so the vswhere/VsDevCmd
    lookup is written exactly once.
#>
function Import-VsDevEnvironment {
  param([string]$Arch)

  $vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswherePath)) {
    throw "vswhere.exe not found at $vswherePath"
  }

  $vsInstallPath = & $vswherePath -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -latest
  if (-not $vsInstallPath) {
    $vsInstallPath = & $vswherePath -products * -requires Microsoft.Component.MSBuild -property installationPath -latest
  }
  if (-not $vsInstallPath) {
    throw "Visual Studio installation not found"
  }

  $devCmdPath = Join-Path $vsInstallPath "Common7\Tools\VsDevCmd.bat"
  if (-not (Test-Path $devCmdPath)) {
    throw "VsDevCmd.bat not found at $devCmdPath"
  }

  Write-Host "Configuring MSVC environment for $Arch"
  $environment = cmd /c "`"$devCmdPath`" -arch=$Arch -no_logo >nul && set"
  if ($LASTEXITCODE -ne 0) {
    throw "VsDevCmd.bat failed for architecture $Arch"
  }

  foreach ($line in $environment) {
    if ($line -match "^(.*?)=(.*)$") {
      Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
    }
  }

  Write-Host "Configured MSVC target: $env:VSCMD_ARG_TGT_ARCH"
}
