<#
.SYNOPSIS
    Diagnoses the LIVE audio-device-loss bug on a real (Scream) render endpoint,
    and validates the fix candidate. Reads the JSON snapshots produced by the
    audio-live-repro workflow and prints a combined verdict.

.DESCRIPTION
    This is the live counterpart to Diagnose-AudioUninstall.ps1. That script
    proves the REGISTRY outcome on a fake seeded endpoint. This one proves the
    LIVE KERNEL outcome on a real virtual audio endpoint installed via Scream,
    so it can demonstrate the actual symptom the user reported: after the app
    uninstall, the endpoint goes INACTIVE/missing in Windows and only a REBOOT
    restores it. The fix candidate replaces the reboot with a single
    `Restart-Service AudioEndpointBuilder`.

    THE DIAGNOSED BUG (what these snapshots must demonstrate)
      helpers/ApoRegistration.cpp::uninstall cycles ONLY the AudioSrv service
      (kAudioServiceName = "AudioSrv", ApoRegistration.cpp:44; stop at :252,
      start at :310). It never cycles AudioEndpointBuilder - the service that
      enumerates/activates endpoints and reads FxProperties to build the APO
      chain - nor audiodg.exe. So after uninstall removes the EQ APO
      (DllUnregisterServer + the EqualizerAPO.dll deletion) and restarts only
      AudioSrv, AudioEndpointBuilder keeps its stale endpoint graph and the
      affected endpoint stays inactive until a reboot rebuilds the graph.

    THE FIX CANDIDATE (what the validation snapshot must confirm)
      Restart-Service -Force AudioEndpointBuilder cascades its dependent
      AudioSrv and forces a LIVE endpoint-graph rebuild, bringing the endpoint
      back to Active WITHOUT a reboot.

    SNAPSHOT PHASES (each is a <phase>-live.json file in $SnapshotDir, written
    by the workflow with a shared snapshot helper):
      30-apo-applied   : after the EQ APO was registered on the real Scream
                         endpoint (baseline-installed state).
      35-stream-forced : after a short WASAPI stream forced audiodg to build the
                         endpoint's APO chain. This is the "endpoint is ACTIVE
                         and the EQ APO is live" reference point.
      50-after-velo    : IMMEDIATELY after the AudioSrv-only uninstall, NO reboot.
                         The REPRODUCTION check reads this.
      70-after-aeb     : after Restart-Service -Force AudioEndpointBuilder, still
                         NO reboot. The FIX-VALIDATION check reads this.

    Each snapshot carries, for the discovered Scream endpoint:
      screamGuid            : the {guid} of the Scream Render endpoint
      activeRenderCount     : DEVICE_STATE_ACTIVE render-endpoint count from
                              IMMDeviceEnumerator (the live-graph signal)
      screamDeviceStateMM   : the MMDevices Render\{guid}\DeviceState DWORD
                              (1 = ACTIVE; other bits = DISABLED/UNPLUGGED/NOTPRESENT)
      screamEndpointState   : IMMDevice::GetState for the Scream endpoint, when
                              still resolvable ("ACTIVE"/"DISABLED"/... or
                              "NOTFOUND" when it no longer enumerates)
      screamFxPointsAtEq    : whether FxProperties still names an EQ APO GUID
      registryClean         : whether the original driver APO GUIDs were restored
                              (no EQ GUID dangling) - the registry is proven clean,
                              so a live disappearance is NOT a registry-delete bug
      win32SoundDeviceStatus: Get-CimInstance Win32_SoundDevice Status strings

    VERDICT
      "(live) REPRODUCED": the Scream endpoint was Active before uninstall and
        became INACTIVE/missing after the AudioSrv-only uninstall (no reboot),
        while the registry is clean.
      "(live) FIX VALIDATED": after Restart-Service AudioEndpointBuilder the
        endpoint returned to Active (no reboot).

    EXIT-CODE CONVENTION (documented loudly here and echoed at runtime):
      exit 0  -> the experiment FULLY SUCCEEDED: the bug REPRODUCED (endpoint
                 went inactive after the AudioSrv-only uninstall) AND the fix
                 VALIDATED (AudioEndpointBuilder restart brought it back). This
                 is the only "everything we set out to prove was proven" state.
      exit 1  -> the endpoint did NOT go inactive after the uninstall. Either the
                 bug did not reproduce on this runner, or Scream behaves
                 differently here. Inspect the uploaded snapshots; the live
                 experiment is inconclusive.
      exit 2  -> the bug REPRODUCED but the fix did NOT validate (endpoint stayed
                 inactive after the AudioEndpointBuilder restart). The fix
                 candidate is insufficient on this runner; inspect snapshots.
      exit 3  -> a required snapshot is missing; cannot diagnose.
    NOTE: this convention is deliberate. Unlike a normal "green = healthy"
    job, green here means "the device-loss bug was reproduced AND the fix
    worked", because the whole purpose is to confirm the diagnosis and the fix
    on a real endpoint. Read the printed RESULT line, not just the colour.

.PARAMETER SnapshotDir
    Directory containing the *-live.json snapshots.

.PARAMETER PreMixGuid
    EqualizerAPO pre-mix APO CLSID string, e.g. {EACD2258-FCAC-4FF4-B36D-419E924A6D79}.

.PARAMETER PostMixGuid
    EqualizerAPO post-mix APO CLSID string, e.g. {EC1CC9CE-FAED-4822-828A-82A81A6F018F}.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SnapshotDir,

    [Parameter(Mandatory = $true)]
    [string]$PreMixGuid,

    [Parameter(Mandatory = $true)]
    [string]$PostMixGuid
)

$ErrorActionPreference = 'Stop'

function Read-Phase {
    param([string]$Phase)
    $jsonPath = Join-Path $SnapshotDir "$Phase-live.json"
    if (-not (Test-Path $jsonPath)) {
        return $null
    }
    return Get-Content -Raw -Path $jsonPath | ConvertFrom-Json
}

# An endpoint counts as "live/active" when IMMDevice::GetState reports ACTIVE,
# or (fallback) the MMDevices DeviceState DWORD is exactly 1 (DEVICE_STATE_ACTIVE).
# When the endpoint no longer enumerates at all, screamEndpointState is "NOTFOUND".
function Test-EndpointActive {
    param($Snapshot)
    if ($null -eq $Snapshot) { return $false }
    if ($Snapshot.screamEndpointState -eq 'ACTIVE') { return $true }
    if ($Snapshot.screamEndpointState -eq 'NOTFOUND') { return $false }
    # Fallback to the registry DeviceState only if the COM state was unavailable
    # (e.g. enumeration threw). 1 == DEVICE_STATE_ACTIVE.
    if ($null -eq $Snapshot.screamEndpointState -or $Snapshot.screamEndpointState -eq '') {
        return ([int]$Snapshot.screamDeviceStateMM -eq 1)
    }
    return $false
}

function Format-Snap {
    param($Snapshot)
    if ($null -eq $Snapshot) { return '<no snapshot>' }
    return ("epState={0} mmState={1} activeRender={2} fxPointsAtEq={3} registryClean={4}" -f `
            $Snapshot.screamEndpointState, $Snapshot.screamDeviceStateMM, `
            $Snapshot.activeRenderCount, $Snapshot.screamFxPointsAtEq, $Snapshot.registryClean)
}

$applied   = Read-Phase '30-apo-applied'
$streamed  = Read-Phase '35-stream-forced'
$afterVelo = Read-Phase '50-after-velo'
$afterAeb  = Read-Phase '70-after-aeb'

Write-Host '====================================================================='
Write-Host ' EqualizerAPO-XT  -  LIVE endpoint device-loss reproduction + fix'
Write-Host '====================================================================='
Write-Host "EQ pre-mix  APO GUID : $PreMixGuid"
Write-Host "EQ post-mix APO GUID : $PostMixGuid"
$screamGuid = ($streamed, $applied, $afterVelo, $afterAeb | Where-Object { $_ } | Select-Object -First 1).screamGuid
Write-Host "Scream endpoint GUID : $screamGuid"
Write-Host ''
Write-Host ("apo applied (30)      : {0}" -f (Format-Snap $applied))
Write-Host ("stream forced (35)    : {0}" -f (Format-Snap $streamed))
Write-Host ("after uninstall (50)  : {0}" -f (Format-Snap $afterVelo))
Write-Host ("after AEB restart (70): {0}" -f (Format-Snap $afterAeb))
Write-Host ''

# The "before uninstall" reference is the stream-forced snapshot (the endpoint
# was actively carrying the EQ APO chain). Fall back to apo-applied if the
# stream-force phase was skipped (best-effort, per the workflow note).
$before = if ($null -ne $streamed) { $streamed } else { $applied }
if ($null -eq $before) {
    Write-Error 'No pre-uninstall snapshot (35-stream-forced / 30-apo-applied) found; cannot diagnose.'
    exit 3
}
if ($null -eq $afterVelo) {
    Write-Error 'No post-uninstall snapshot (50-after-velo) found; cannot diagnose.'
    exit 3
}

$beforeActive    = Test-EndpointActive $before
$afterVeloActive = Test-EndpointActive $afterVelo
$afterAebActive  = if ($null -ne $afterAeb) { Test-EndpointActive $afterAeb } else { $false }

# The registry being CLEAN after uninstall is what makes the live disappearance
# the AudioEndpointBuilder/stale-graph bug rather than a registry-delete bug. We
# report it but do not gate on it (a not-clean registry is a different finding,
# covered by Diagnose-AudioUninstall.ps1 / Diagnose-DanglingOnFailure.ps1).
$registryClean = [bool]$afterVelo.registryClean

$verdicts = New-Object System.Collections.Generic.List[string]

$reproduced = ($beforeActive -and -not $afterVeloActive)
$fixValidated = ($reproduced -and $afterAebActive)

if (-not $beforeActive) {
    $verdicts.Add('PRECONDITION NOT MET: the Scream endpoint was not Active before the uninstall. The live experiment cannot run (Scream may not have activated on this runner, or the stream-force did not bring it Active). Inspect 30/35 snapshots.')
}
elseif ($reproduced) {
    $verdicts.Add('(live) REPRODUCED: the Scream endpoint was ACTIVE before the uninstall and went INACTIVE/missing immediately after the AudioSrv-only uninstall, WITHOUT a reboot.')
    if ($registryClean) {
        $verdicts.Add('  Registry is CLEAN after uninstall (original driver APO GUIDs restored, no EQ GUID dangling). The live loss is therefore the stale endpoint-graph bug: ApoRegistration::uninstall cycled only AudioSrv, never AudioEndpointBuilder.')
    }
    else {
        $verdicts.Add('  WARNING: registry is NOT clean after uninstall (an EQ APO GUID is still named or originals were not restored). The live loss may be compounded by a dangling-reference bug; see Diagnose-AudioUninstall.ps1.')
    }
}
else {
    $verdicts.Add('(live) NOT REPRODUCED: the Scream endpoint stayed ACTIVE after the AudioSrv-only uninstall (no reboot). Either the bug did not reproduce on this runner or Scream behaves differently here.')
}

if ($reproduced) {
    if ($null -eq $afterAeb) {
        $verdicts.Add('(live) FIX NOT EVALUATED: no post-AudioEndpointBuilder-restart snapshot (70-after-aeb) found.')
    }
    elseif ($fixValidated) {
        $verdicts.Add('(live) FIX VALIDATED: after Restart-Service -Force AudioEndpointBuilder the Scream endpoint returned to ACTIVE, WITHOUT a reboot. The fix candidate restores the endpoint live.')
    }
    else {
        $verdicts.Add('(live) FIX NOT VALIDATED: the Scream endpoint stayed INACTIVE/missing even after the AudioEndpointBuilder restart. The fix candidate is insufficient on this runner.')
    }
}

Write-Host '--------------------------------------------------------------------'
Write-Host ' VERDICT'
Write-Host '--------------------------------------------------------------------'
foreach ($v in $verdicts) {
    Write-Host " - $v"
}
Write-Host ''

# Exit-code convention (see the .DESCRIPTION header for the full rationale).
if (-not $beforeActive) {
    Write-Host 'RESULT: INCONCLUSIVE - endpoint was not Active before uninstall (see exit 1).'
    exit 1
}
if (-not $reproduced) {
    Write-Host 'RESULT: INCONCLUSIVE - bug did NOT reproduce on this runner (exit 1). Inspect the snapshots.'
    exit 1
}
if (-not $fixValidated) {
    Write-Host 'RESULT: REPRODUCED but FIX NOT VALIDATED (exit 2). Inspect the snapshots.'
    exit 2
}
Write-Host 'RESULT: live device-loss bug REPRODUCED and the AudioEndpointBuilder-restart fix VALIDATED (exit 0). Experiment fully succeeded.'
exit 0
