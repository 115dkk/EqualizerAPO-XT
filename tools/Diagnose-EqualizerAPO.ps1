<#
.SYNOPSIS
    Read-only diagnostics for EqualizerAPO-XT installs.
    Dumps install path, ACLs, COM registration, MMDevices APO chains, and
    flags the conditions that cause "GetMixFormat / Initialize access denied"
    in Device Selector.

.DESCRIPTION
    Run from any PowerShell. Elevation is not required - this script does not
    modify the system.

    The most common failure mode on a Velopack install is:
      audiodg.exe (LOCAL SERVICE) cannot read EqualizerAPO.dll because the
      DLL lives under %LocalAppData% and the default ACL only grants the
      installing user and Administrators. The DLL load fails inside the
      audio engine, so any IAudioClient call against the affected device
      returns E_ACCESSDENIED (Korean: "액세스가 거부되었습니다").

    Use Repair-EqualizerAPO.ps1 (elevated) to apply the LOCAL SERVICE / Users
    ACL grants if this diagnostics report flags the install root as broken.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\Diagnose-EqualizerAPO.ps1

.EXAMPLE
    .\Diagnose-EqualizerAPO.ps1 > eapo-report.txt
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'

$preGuid  = '{eacd2258-fcac-4ff4-b36d-419e924a6d79}'
$postGuid = '{ec1cc9ce-faed-4822-828a-82a81a6f018f}'

function Write-Section { param([string]$Title) "`n=== $Title ===" }

function Get-RegValueSafe {
    param([string]$Path, [string]$Name)
    try { (Get-Item -LiteralPath $Path -ErrorAction Stop).GetValue($Name) }
    catch { $null }
}

function Test-LocalServiceAccess {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    $acl = Get-Acl -LiteralPath $Path
    $hasLs = $acl.Access | Where-Object {
        $_.IdentityReference.Value -like '*LOCAL SERVICE*' -and
        $_.AccessControlType -eq 'Allow'
    }
    $hasUsersRx = $acl.Access | Where-Object {
        $_.IdentityReference.Value -like '*\Users' -and
        $_.AccessControlType -eq 'Allow' -and
        (($_.FileSystemRights -band [System.Security.AccessControl.FileSystemRights]::ReadAndExecute) -ne 0)
    }
    [PSCustomObject]@{
        LocalServiceAllowed = [bool]$hasLs
        UsersReadAndExecute = [bool]$hasUsersRx
    }
}

Write-Section 'Environment'
"Time:           $(Get-Date -Format o)"
"Current user:   $([System.Security.Principal.WindowsIdentity]::GetCurrent().Name)"
"Is elevated:    $(([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))"
"PowerShell:     $($PSVersionTable.PSVersion)"
"OS:             $((Get-CimInstance Win32_OperatingSystem).Caption) ($((Get-CimInstance Win32_OperatingSystem).Version))"

Write-Section 'EqualizerAPO registry'
$appReg          = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO'
$installPath     = Get-RegValueSafe $appReg 'InstallPath'
$configPath      = Get-RegValueSafe $appReg 'ConfigPath'
$enableTrace     = Get-RegValueSafe $appReg 'EnableTrace'
$disableProtAdg  = Get-RegValueSafe 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio' 'DisableProtectedAudioDG'

"InstallPath:              $installPath"
"ConfigPath:               $configPath"
"EnableTrace:              $enableTrace"
"DisableProtectedAudioDG:  $disableProtAdg"

if (-not $installPath) {
    Write-Warning "EqualizerAPO is not registered on this machine. Run the installer first."
    return
}

Write-Section 'APO COM registration'
foreach ($pair in @(@{Name='PreMix'; Guid=$preGuid}, @{Name='PostMix'; Guid=$postGuid})) {
    $clsidPath = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$($pair.Guid)\InprocServer32"
    $dll = Get-RegValueSafe $clsidPath ''
    "$($pair.Name) InprocServer32: $dll"
    if ($dll) {
        "  DLL exists:            $(Test-Path -LiteralPath $dll)"
        if (-not (Test-Path -LiteralPath $dll)) {
            Write-Warning "$($pair.Name) APO DLL is missing on disk. Re-run install."
        }
    }
}

Write-Section 'Install location class'
$profileDir   = [Environment]::GetFolderPath('UserProfile')
$localAppData = [Environment]::GetFolderPath('LocalApplicationData')
$inProfile    = $installPath.StartsWith($profileDir,   [StringComparison]::OrdinalIgnoreCase) -or
                $installPath.StartsWith($localAppData, [StringComparison]::OrdinalIgnoreCase)
if ($inProfile) {
    Write-Warning "InstallPath lives under the installing user's profile. audiodg.exe (LOCAL SERVICE) must be granted RX on this tree, otherwise the APO will fail to load."
} else {
    "InstallPath is outside the user's profile."
}

Write-Section 'Install root ACL'
"Path: $installPath"
$installAcl = Test-LocalServiceAccess $installPath
if ($null -ne $installAcl) {
    "LOCAL SERVICE allowed: $($installAcl.LocalServiceAllowed)"
    "Users RX:              $($installAcl.UsersReadAndExecute)"
    if (-not $installAcl.LocalServiceAllowed) {
        Write-Warning "MISSING: LOCAL SERVICE grant on install root. audiodg.exe cannot load EqualizerAPO.dll. This causes GetMixFormat / Initialize access denied."
    }
    if (-not $installAcl.UsersReadAndExecute) {
        Write-Warning "MISSING: Users RX on install root. Editor/DeviceSelector may not start for non-admin users."
    }
} else {
    Write-Warning "Install root does not exist: $installPath"
}

Write-Section 'Config dir ACL'
if ($configPath) {
    "Path: $configPath"
    $configAcl = Test-LocalServiceAccess $configPath
    if ($null -ne $configAcl) {
        "LOCAL SERVICE allowed: $($configAcl.LocalServiceAllowed)"
        "Users RX:              $($configAcl.UsersReadAndExecute)"
        if (-not $configAcl.LocalServiceAllowed) {
            Write-Warning "MISSING: LOCAL SERVICE grant on config dir. audiodg cannot read config.txt and the APO will run with empty filters."
        }
    } else {
        Write-Warning "Config dir does not exist: $configPath"
    }
}

Write-Section 'EqualizerAPO APO chains on devices'
$fxGuidNames = @(
    @{Name='LFX'; Id='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1'},
    @{Name='GFX'; Id='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2'},
    @{Name='SFX'; Id='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5'},
    @{Name='MFX'; Id='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6'},
    @{Name='EFX'; Id='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7'}
)
$mmRoots = @(
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render',
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture'
)

$attached = @()
foreach ($root in $mmRoots) {
    if (-not (Test-Path -LiteralPath $root)) { continue }
    foreach ($dev in Get-ChildItem -LiteralPath $root -ErrorAction SilentlyContinue) {
        $fx = Join-Path $dev.PSPath 'FxProperties'
        if (-not (Test-Path -LiteralPath $fx)) { continue }
        $slots = @()
        foreach ($f in $fxGuidNames) {
            $v = Get-RegValueSafe $fx $f.Id
            if ($v -and ($v -ieq $preGuid -or $v -ieq $postGuid)) {
                $slot = if ($v -ieq $preGuid) { "$($f.Name)=PreMix" } else { "$($f.Name)=PostMix" }
                $slots += $slot
            }
        }
        if ($slots.Count -gt 0) {
            $props      = Join-Path $dev.PSPath 'Properties'
            $connection = Get-RegValueSafe $props '{a45c254e-df1c-4efd-8020-67d146a850e0},2'
            $deviceName = Get-RegValueSafe $props '{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
            $attached += [PSCustomObject]@{
                Kind       = Split-Path $root -Leaf
                Device     = $deviceName
                Connection = $connection
                Slots      = ($slots -join ', ')
            }
        }
    }
}
if ($attached.Count -eq 0) {
    "No devices currently have EqualizerAPO in their APO chain."
} else {
    $attached | Format-Table -AutoSize | Out-String -Width 220
}

Write-Section 'audiodg.exe state'
$audiodg = Get-Process -Name audiodg -ErrorAction SilentlyContinue
if ($audiodg) {
    $audiodg | Select-Object Id, StartTime, ProcessName | Format-Table -AutoSize | Out-String
} else {
    "audiodg.exe is not running (audio engine is idle)."
}

Write-Section 'Suggested next step'
if ($installAcl -and -not $installAcl.LocalServiceAllowed) {
@"
The install root is missing LOCAL SERVICE access. To repair an existing
install without reinstalling, run from an ELEVATED PowerShell:

  .\Repair-EqualizerAPO.ps1

That script applies the same ACL grants the install hook would apply on a
clean install, then restarts Audiosrv so audiodg picks up the change.
"@
} else {
    "No obvious ACL problems detected. If Device Selector still reports access denied, capture an ETW trace (logman) or ProcMon trace filtered to audiodg.exe and Path containing EqualizerAPO."
}
