<#
.SYNOPSIS
    Repairs an existing EqualizerAPO-XT install whose APO DLL is unreadable
    to audiodg.exe (LOCAL SERVICE), without reinstalling.

.DESCRIPTION
    Applies the same ACL grants the install hook would set on a clean install:
      - install root: LOCAL SERVICE RX recursive, Users RX recursive
      - config dir:   Users F recursive, LOCAL SERVICE M recursive
    Then restarts Audiosrv so the audio engine reloads the APO with the
    corrected ACL.

    Must be run elevated. If the install hook on a fresh install already
    applies these grants (as of the v0.3 fix), this script is only needed
    for installs that predate that fix.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\Repair-EqualizerAPO.ps1

.NOTES
    Restarting Audiosrv interrupts any active audio playback for a few seconds.
#>

[CmdletBinding(SupportsShouldProcess)]
param(
    [switch]$SkipServiceRestart
)

$ErrorActionPreference = 'Stop'

function Assert-Elevated {
    $current = [Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $current.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "This script must be run from an elevated PowerShell. Right-click PowerShell > Run as administrator."
    }
}

function Invoke-Icacls {
    param(
        [string]$Path,
        [string[]]$Grants
    )
    $args = @("`"$Path`"")
    foreach ($g in $Grants) { $args += '/grant'; $args += $g }
    $args += '/T'; $args += '/C'; $args += '/Q'
    Write-Host "  icacls $($args -join ' ')"
    $output = & icacls.exe @args 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "icacls returned $LASTEXITCODE for $Path"
        $output | ForEach-Object { Write-Warning "    $_" }
    }
}

Assert-Elevated

$apoRegPath = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO'
if (-not (Test-Path $apoRegPath)) {
    throw "EqualizerAPO is not registered. Install it first."
}

$installPath = (Get-ItemProperty $apoRegPath -Name InstallPath -ErrorAction Stop).InstallPath
$configPath  = (Get-ItemProperty $apoRegPath -Name ConfigPath  -ErrorAction Stop).ConfigPath

if (-not (Test-Path -LiteralPath $installPath)) {
    throw "InstallPath does not exist: $installPath"
}

Write-Host "InstallPath: $installPath"
Write-Host "ConfigPath:  $configPath"

if ($PSCmdlet.ShouldProcess($installPath, 'Grant LOCAL SERVICE RX recursive')) {
    Write-Host ""
    Write-Host "Granting LOCAL SERVICE (RX) and Users (RX) on install root..."
    Invoke-Icacls -Path $installPath -Grants @(
        '*S-1-5-19:(OI)(CI)RX',
        '*S-1-5-32-545:(OI)(CI)RX'
    )
}

if ($configPath -and (Test-Path -LiteralPath $configPath)) {
    if ($PSCmdlet.ShouldProcess($configPath, 'Grant Users F + LOCAL SERVICE M recursive')) {
        Write-Host ""
        Write-Host "Granting Users (F) and LOCAL SERVICE (M) on config dir..."
        Invoke-Icacls -Path $configPath -Grants @(
            '*S-1-5-32-545:(OI)(CI)F',
            '*S-1-5-19:(OI)(CI)M'
        )
    }
}

if (-not $SkipServiceRestart) {
    if ($PSCmdlet.ShouldProcess('Audiosrv', 'Restart so audiodg reloads the APO')) {
        Write-Host ""
        Write-Host "Restarting Audiosrv..."
        Restart-Service -Name Audiosrv -Force
        Write-Host "Done. Open Device Selector and re-run the APO installation check."
    }
} else {
    Write-Host ""
    Write-Host "Skipped Audiosrv restart. Restart it manually (or reboot) for audiodg to pick up the new ACL."
}
