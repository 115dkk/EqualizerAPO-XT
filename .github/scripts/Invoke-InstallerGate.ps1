<#
.SYNOPSIS
    Runs a packed installer the way a user's machine does, before it is
    published: install, update, self-test, uninstall.

.DESCRIPTION
    The product reaches users through automatic updates, and the first time a
    release's installer and its Velopack hooks ran used to be on a user's
    machine: CI executed --veloapp-install only through the capture gate's
    unpacked artifact, and never --veloapp-updated, --veloapp-obsolete or
    --veloapp-uninstall, nor the packed layout (Qt plugins under qt\, the
    sample configs, tools\) that exists only after `vpk pack`
    (TECH_DEBT_AUDIT TD-04, audit #467).

    Rounds, each followed by its checks:

      update   Install the latest published release of the channel, then
               apply the new package over it with Update.exe. This runs the
               old version's --veloapp-obsolete and the new version's
               --veloapp-updated. Skipped, and said so, when the channel has
               no published release to start from.
      selftest Run the installed Editor's --selftest-vst from the installed
               folder, so the packed layout has to load.
      remove   Update.exe --uninstall: --veloapp-uninstall. The APO's COM
               class must be gone and the audio service back in the state it
               was in before the gate touched the machine.
      fresh    Run the new Setup.exe on the now clean machine:
               --veloapp-install of the new version. Checked, self-tested and
               removed again.

    "Installed" means: Update.exe and current\ exist, current\ carries the
    files named in the plan, the APO's COM class points at the installed
    EqualizerAPO.dll, and HKLM\SOFTWARE\EqualizerAPO names an existing install
    folder and an existing config folder with a config.txt.

    The runner must be elevated (GitHub's Windows runners are): the hooks
    write HKLM and restart the audio service. Never run this on a machine
    whose audio or installed copy matters.

    -PlanOnly returns the rounds and the file and registry expectations, for
    Pester.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    # Empty means the manifest's Primary variant's channel.
    [AllowEmptyString()] [string] $Channel = "",
    # Where `New-VelopackRelease.ps1 -Phase Pack` left the channel's output.
    # Empty means <WorkspaceRoot>\velopack-output\<channel>.
    [AllowEmptyString()] [string] $PackOutputDirectory = "",
    [Parameter(Mandatory)] [string] $PackVersion,
    [Parameter(Mandatory)] [string] $Repository,
    [switch] $SkipUpdateRound,
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "ReleaseAssets.psm1") -Force
Import-Module (Join-Path $PSScriptRoot "ApoEndpointHarness.psm1") -Force
Import-Module (Join-Path $PSScriptRoot "Provisioning.psm1") -Force

$manifest = Import-PowerShellDataFile -Path (Join-Path $PSScriptRoot "..\simd-variants.psd1")
if (-not $Channel) { $Channel = (Get-PrimaryVariant -Manifest $manifest).Channel }
if (-not $PackOutputDirectory) { $PackOutputDirectory = Join-Path $WorkspaceRoot "velopack-output\$Channel" }

$packId = Get-VelopackPackId -Channel $Channel
$contract = Get-ApoEndpointContract
$rounds = @()
if (-not $SkipUpdateRound) { $rounds += "update", "selftest", "remove" }
$rounds += "fresh", "selftest", "remove"

$plan = [pscustomobject]@{
    Channel       = $Channel
    PackId        = $packId
    SetupPath     = Join-Path $PackOutputDirectory (Get-SetupAssetName -Channel $Channel)
    PackagePath   = Join-Path $PackOutputDirectory "$packId-$PackVersion-$Channel-full.nupkg"
    InstallRoot   = Join-Path $env:LOCALAPPDATA $packId
    Rounds        = $rounds
    # Relative to <InstallRoot>\current. The Qt platform plugin sits under
    # qt\ only in a packed install; tools\ and config\ are packaging content.
    RequiredFiles = @(
        "Editor.exe", "DeviceSelector.exe", "EqualizerAPO.dll", "Benchmark.exe",
        "EqualizerAPOHost.exe", "EqualizerAPOAsio.dll",
        "qt\platforms\qwindows.dll", "config\config.txt", "tools\Repair-EqualizerAPO.ps1"
    )
    ClassKeys     = @($contract.EQ_PREMIX_GUID, $contract.EQ_POSTMIX_GUID |
            ForEach-Object { "HKLM:\SOFTWARE\Classes\CLSID\$_\InprocServer32" })
    AppKey        = "HKLM:\SOFTWARE\EqualizerAPO"
    SelfTest      = @("--selftest-vst")
    TimeoutSeconds = @{ Setup = 600; Update = 600; SelfTest = 300; Uninstall = 300 }
}
if ($PlanOnly) { return $plan }

function Get-AudioServiceState {
    $service = Get-Service -Name Audiosrv -ErrorAction SilentlyContinue
    if ($service) { return "$($service.Status)" }
    return "Absent"
}

function Get-RegisteredServer([string] $classKey) {
    if (-not (Test-Path -LiteralPath $classKey)) { return $null }
    return (Get-Item -LiteralPath $classKey).GetValue("")
}

function Assert-Installed([string] $label) {
    $current = Join-Path $plan.InstallRoot "current"
    if (-not (Test-Path -LiteralPath (Join-Path $plan.InstallRoot "Update.exe"))) {
        throw "[$label] Update.exe is missing under $($plan.InstallRoot)"
    }
    foreach ($relative in $plan.RequiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $current $relative))) {
            throw "[$label] the installed folder lacks $relative"
        }
    }
    $expectedServer = Join-Path $current "EqualizerAPO.dll"
    foreach ($classKey in $plan.ClassKeys) {
        $server = Get-RegisteredServer $classKey
        if (-not $server) { throw "[$label] the APO's COM class is not registered: $classKey" }
        if ([System.IO.Path]::GetFullPath($server) -ne [System.IO.Path]::GetFullPath($expectedServer)) {
            throw "[$label] $classKey points at '$server', expected '$expectedServer'"
        }
    }
    $app = Get-ItemProperty -LiteralPath $plan.AppKey -ErrorAction Stop
    if (-not $app.InstallPath -or -not (Test-Path -LiteralPath $app.InstallPath)) {
        throw "[$label] InstallPath '$($app.InstallPath)' does not name an existing folder"
    }
    if (-not $app.ConfigPath -or -not (Test-Path -LiteralPath (Join-Path $app.ConfigPath "config.txt"))) {
        throw "[$label] ConfigPath '$($app.ConfigPath)' holds no config.txt"
    }
    Write-Host "[$label] installed: $current (InstallPath=$($app.InstallPath), ConfigPath=$($app.ConfigPath))"
}

function Assert-AudioService([string] $label) {
    # The hooks stop and start the audio service; give it a moment to settle
    # before comparing with the state the machine started in.
    $deadline = (Get-Date).AddSeconds(60)
    do {
        $state = Get-AudioServiceState
        if ($state -eq $script:audioBaseline) { return }
        Start-Sleep -Seconds 2
    } while ((Get-Date) -lt $deadline)
    throw "[$label] the audio service is '$state'; it was '$script:audioBaseline' before the gate"
}

function Assert-Removed([string] $label) {
    foreach ($classKey in $plan.ClassKeys) {
        if (Test-Path -LiteralPath $classKey) { throw "[$label] the APO's COM class is still registered: $classKey" }
    }
    $current = Join-Path $plan.InstallRoot "current"
    # Velopack removes the folder a moment after Update.exe returns.
    $deadline = (Get-Date).AddSeconds(60)
    while ((Test-Path -LiteralPath $current) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
    if (Test-Path -LiteralPath $current) { throw "[$label] $current is still there after the uninstall" }
    Assert-AudioService $label
    Write-Host "[$label] removed"
}

function Invoke-Checked([string] $label, [string] $file, [string[]] $arguments, [int] $timeoutSeconds, [string] $workingDirectory) {
    Write-Host "[$label] $file $($arguments -join ' ')"
    $result = Invoke-ExternalProgram $file $arguments $timeoutSeconds $workingDirectory
    if ($result.TimedOut) { throw "[$label] $(Split-Path $file -Leaf) did not finish within $timeoutSeconds s" }
    if ($result.ExitCode -ne 0) { throw "[$label] $(Split-Path $file -Leaf) exited with $($result.ExitCode)" }
}

function Get-InstalledVersion {
    # Velopack records the installed package in current\sq.version (a nuspec).
    $path = Join-Path $plan.InstallRoot "current\sq.version"
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $match = [regex]::Match((Get-Content -Raw -LiteralPath $path), "<version>([^<]+)</version>")
    if ($match.Success) { return $match.Groups[1].Value }
    return $null
}

if (-not (Test-Path -LiteralPath $plan.SetupPath)) { throw "The pack output is missing $($plan.SetupPath)" }
# vpk names the full package itself; the plan states the expected name, and a
# name that drifted is still found as long as exactly one full package of this
# version is there.
$package = $plan.PackagePath
if (-not (Test-Path -LiteralPath $package)) {
    $found = @(Get-ChildItem -LiteralPath $PackOutputDirectory -Filter "*-full.nupkg" |
            Where-Object { $_.Name -like "*$PackVersion*" })
    if ($found.Count -ne 1) {
        throw "Expected one full package for $PackVersion in $PackOutputDirectory, found $($found.Count): $($found.Name -join ', ')"
    }
    Write-Warning "The full package is named $($found[0].Name), not $(Split-Path $plan.PackagePath -Leaf)."
    $package = $found[0].FullName
}
foreach ($classKey in $plan.ClassKeys) {
    if (Test-Path -LiteralPath $classKey) {
        throw "This machine already has the APO registered ($classKey); the gate needs a clean machine."
    }
}
$script:audioBaseline = Get-AudioServiceState
Write-Host "Audio service before the gate: $script:audioBaseline"
$update = Join-Path $plan.InstallRoot "Update.exe"
$editor = Join-Path $plan.InstallRoot "current\Editor.exe"
$updateRoundRan = $false

$position = 0
foreach ($round in $plan.Rounds) {
    $position++
    $label = "$position/$($plan.Rounds.Count) $round"
    switch ($round) {
        "update" {
            $download = Join-Path $WorkspaceRoot "installer-gate\previous"
            $previous = & (Join-Path $PSScriptRoot "Install-ReleaseBuild.ps1") -Repository $Repository -Tag "" `
                -Channel $Channel -DownloadDirectory $download -AllowMissing -PassThru
            if (-not $previous) {
                Write-Warning "[$label] no published release for channel $Channel to update from; the update round is skipped."
                break
            }
            Assert-Installed "$label (previous release)"
            $before = Get-InstalledVersion
            if ($before -eq $PackVersion) {
                # A resumed release run: the newest published release already
                # is this version, so there is nothing to update to. The
                # rounds that follow self-test and remove what was installed.
                Write-Warning "[$label] the newest published release already is $PackVersion; nothing to apply."
                break
            }
            Invoke-Checked $label $update @("apply", "--norestart", "--package", $package) `
                $plan.TimeoutSeconds.Update $plan.InstallRoot
            $after = Get-InstalledVersion
            if ($after -ne $PackVersion) {
                throw "[$label] the installed version is '$after' after the update from '$before'; expected '$PackVersion'"
            }
            Assert-Installed $label
            Assert-AudioService $label
            $updateRoundRan = $true
        }
        "fresh" {
            Invoke-Checked $label $plan.SetupPath @("--silent") $plan.TimeoutSeconds.Setup $null
            Assert-Installed $label
            Assert-AudioService $label
        }
        "selftest" {
            if (-not (Test-Path -LiteralPath $editor)) {
                Write-Host "[$label] nothing installed (the round before was skipped)"
                break
            }
            Invoke-Checked $label $editor $plan.SelfTest $plan.TimeoutSeconds.SelfTest (Split-Path $editor -Parent)
        }
        "remove" {
            if (-not (Test-Path -LiteralPath $update)) {
                Write-Host "[$label] nothing installed (the round before was skipped)"
                break
            }
            Invoke-Checked $label $update @("--uninstall", "--silent") $plan.TimeoutSeconds.Uninstall $plan.InstallRoot
            Assert-Removed $label
        }
    }
}

Write-Host "Installer gate passed for $Channel $PackVersion (update round ran: $updateRoundRan)."
$global:LASTEXITCODE = 0
