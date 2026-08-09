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
      services/install/ApoRegistration.cpp::uninstall cycles ONLY the AudioSrv service
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
    [string]$PostMixGuid,

    # reproduce    : prove the bug on an UNFIXED build (exit 0 = endpoint went
    #               INACTIVE after the AudioSrv-only uninstall AND an
    #               AudioEndpointBuilder restart brought it back).
    # expect-fixed : verify a FIXED release (exit 0 = endpoint STAYED ACTIVE
    #               through the real uninstall AND no EQ CLSID was left dangling
    #               in FxProperties; exit 2 = the endpoint went inactive OR the
    #               registry stayed dirty - on OS build 26100+ the dirty case is
    #               the NOKEY-branch deleteKey failing on the OS-created
    #               FxProperties subkeys, issue #189).
    [Parameter(Mandatory = $false)]
    [ValidateSet('reproduce', 'expect-fixed')]
    [string]$Mode = 'reproduce'
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

# Tri-state activity of the endpoint from a snapshot: 'Active', 'Inactive', or
# 'Unknown'. The live COM state (IMMDevice::GetState via the snapshot helper) is
# authoritative: 'ACTIVE' -> Active; 'NOTFOUND'/'DISABLED'/'UNPLUGGED'/
# 'NOTPRESENT' -> Inactive. 'ENUM_FAILED' means the COM enumerator itself threw
# even after retries, so we genuinely do not know -> Unknown (NOT a device-loss
# signal; treating it as Inactive would produce a false "reproduced"). The
# registry DeviceState DWORD is deliberately NOT used as an active signal,
# because it stayed 1 (ACTIVE) even when the live endpoint was gone - that
# registry-vs-live mismatch is the very bug being measured.
function Get-EndpointActivity {
    param($Snapshot)
    if ($null -eq $Snapshot) { return 'Unknown' }
    switch ($Snapshot.screamEndpointState) {
        'ACTIVE'      { return 'Active' }
        'NOTFOUND'    { return 'Inactive' }
        'DISABLED'    { return 'Inactive' }
        'UNPLUGGED'   { return 'Inactive' }
        'NOTPRESENT'  { return 'Inactive' }
        'ENUM_FAILED' { return 'Unknown' }
        default       { return 'Unknown' }
    }
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
$control   = Read-Phase '45-control'
$afterVelo = Read-Phase '50-after-velo'
$afterAeb  = Read-Phase '70-after-aeb'

Write-Host '====================================================================='
Write-Host ' EqualizerAPO-XT  -  LIVE endpoint device-loss reproduction + fix'
Write-Host '====================================================================='
Write-Host "EQ pre-mix  APO GUID : $PreMixGuid"
Write-Host "EQ post-mix APO GUID : $PostMixGuid"
$screamGuid = ($streamed, $applied, $control, $afterVelo, $afterAeb | Where-Object { $_ } | Select-Object -First 1).screamGuid
Write-Host "Scream endpoint GUID : $screamGuid"
Write-Host ''
Write-Host ("apo applied (30)        : {0}" -f (Format-Snap $applied))
Write-Host ("stream forced (35)      : {0}" -f (Format-Snap $streamed))
Write-Host ("control AudioSrv (45)   : {0}" -f (Format-Snap $control))
Write-Host ("after uninstall (50)    : {0}" -f (Format-Snap $afterVelo))
Write-Host ("after AEB restart (70)  : {0}" -f (Format-Snap $afterAeb))
Write-Host ''

# The "before uninstall" reference is the LATEST pre-uninstall snapshot that is
# definitively Active with the EQ APO live. Prefer the control (45, closest to
# the uninstall and proves the endpoint survived an AudioSrv restart), then the
# stream-forced (35), then apo-applied (30). This avoids the run-3 false
# negative where the stream-forced snapshot transiently ENUM_FAILED.
$before = $null
foreach ($cand in @($control, $streamed, $applied)) {
    if ($null -ne $cand -and (Get-EndpointActivity $cand) -eq 'Active' -and $cand.screamFxPointsAtEq) { $before = $cand; break }
}
if ($null -eq $before) {
    # None was definitively Active+EQ-live; keep any non-null pre-uninstall
    # snapshot so the verdict can explain why the precondition was not met.
    $before = ($control, $streamed, $applied | Where-Object { $_ } | Select-Object -First 1)
}
if ($null -eq $before) {
    Write-Error 'No pre-uninstall snapshot (45/35/30) found; cannot diagnose.'
    exit 3
}
if ($null -eq $afterVelo) {
    Write-Error 'No post-uninstall snapshot (50-after-velo) found; cannot diagnose.'
    exit 3
}

$beforeState    = Get-EndpointActivity $before
$controlState   = Get-EndpointActivity $control
$afterVeloState = Get-EndpointActivity $afterVelo
$afterAebState  = Get-EndpointActivity $afterAeb

# The registry being CLEAN after uninstall is what makes the live disappearance
# the AudioEndpointBuilder/stale-graph bug rather than a registry-delete bug.
# In reproduce mode it is reported but not gated on (a not-clean registry is a
# different finding). In expect-fixed mode it IS part of the pass criteria:
# on OS build 26100+ (Windows 11 24H2 codebase / Server 2025) the OS creates
# subkeys under FxProperties, DeviceAPOInfo::uninstall's NOKEY-branch deleteKey
# fails on them, and the uninstall then leaves the removed EQ CLSIDs dangling
# in FxProperties (issue #189). A release only counts as fixed when the
# endpoint survives AND no EQ GUID is left behind.
$registryClean = [bool]$afterVelo.registryClean

# Whether the EQ APO was actually live before the uninstall. If it was never
# applied, the bug's precondition (a dangling EQ APO reference after removal)
# never existed, so a "still active" result is meaningless.
$eqWasApplied = [bool]$before.screamFxPointsAtEq

$verdicts = New-Object System.Collections.Generic.List[string]

# reproduced requires a DEFINITE before=Active -> after=Inactive transition.
# Any 'Unknown' (ENUM_FAILED) on the gating snapshots makes the run inconclusive
# rather than a false positive/negative.
$reproduced   = ($beforeState -eq 'Active' -and $afterVeloState -eq 'Inactive')
$fixValidated = ($reproduced -and $afterAebState -eq 'Active')

# CONTROL: an AudioSrv-only restart with the EQ APO STILL installed must leave
# the endpoint Active. If it goes Inactive, the AudioSrv restart itself disrupts
# the endpoint and the post-uninstall loss cannot be cleanly attributed to the
# APO removal -> confounded. ('Unknown' weakens isolation but does not confound.)
$controlConfounded = ($controlState -eq 'Inactive')

$inconclusive = $false
if (-not $eqWasApplied) {
    $verdicts.Add('SETUP INVALID: the EQ APO was not actually applied to the Scream endpoint before the uninstall (screamFxPointsAtEq=False at the pre-uninstall snapshot). The bug precondition was never established; inspect step 30. This is a harness problem, not a property of the audio code.')
    $inconclusive = $true
}
elseif ($beforeState -ne 'Active') {
    $verdicts.Add("PRECONDITION NOT MET: the Scream endpoint was not confirmed Active before the uninstall (state=$beforeState). " +
        "If 'Unknown', the COM enumerator failed even after retries; if 'Inactive', Scream did not stay active. Inspect 30/35/45 snapshots.")
    $inconclusive = $true
}
elseif ($controlConfounded) {
    $verdicts.Add("CONTROL FAILED (confounded): an AudioSrv-only restart with the EQ APO still installed ALSO made the endpoint inactive (control state=$controlState). The AudioSrv restart itself disrupts the Scream endpoint on this runner, so the post-uninstall loss cannot be cleanly attributed to the APO removal. Inspect the 45-control snapshot.")
    $inconclusive = $true
}
elseif ($afterVeloState -eq 'Unknown') {
    $verdicts.Add('INCONCLUSIVE: the endpoint state could not be read after the uninstall (COM enumerator failed even after retries). Cannot tell reproduced from not-reproduced; inspect the 50-after-velo snapshot.')
    $inconclusive = $true
}
elseif ($Mode -eq 'expect-fixed') {
    # Validating a FIXED release: the endpoint must STAY ACTIVE through the real
    # uninstall (the shipped ApoRegistration::uninstall restarts AudioEndpointBuilder)
    # AND the registry must be clean afterwards (no EQ CLSID left dangling -
    # the 26100+ FxProperties-subkey regression, issue #189).
    if ($afterVeloState -eq 'Active') {
        $verdicts.Add('(fixed) PASS: the endpoint STAYED ACTIVE through the real uninstall - with audio in use and no reboot. The shipped build restarts AudioEndpointBuilder during uninstall, so removing the APO no longer drops the device.')
    }
    else {
        $verdicts.Add("(fixed) FAIL: the endpoint went INACTIVE/missing after the real uninstall (after-state=$afterVeloState). The AudioEndpointBuilder restart did not take effect during uninstall - the fix is absent or ineffective in this build.")
    }
    if ($registryClean) {
        $verdicts.Add('(fixed) Registry is CLEAN after uninstall: no EQ APO GUID left in FxProperties.')
    }
    else {
        $verdicts.Add('(fixed) FAIL: the uninstall left an EQ APO GUID dangling in FxProperties. On OS build 26100+ this is the NOKEY-branch deleteKey failing on the OS-created FxProperties subkeys (issue #189); the per-device restore never completed.')
    }
}
elseif ($reproduced) {
    $verdicts.Add('(live) REPRODUCED: the Scream endpoint was ACTIVE (with the EQ APO live) before the uninstall and went INACTIVE/missing immediately after the AudioSrv-only uninstall, WITHOUT a reboot.')
    if ($controlState -eq 'Active') {
        $verdicts.Add('  CONTROL PASSED: an AudioSrv-only restart with the EQ APO still installed kept the endpoint ACTIVE, so the AudioSrv restart by itself does not remove the endpoint. The loss is isolated to the APO removal leaving a stale endpoint graph.')
    }
    elseif ($controlState -eq 'Unknown') {
        $verdicts.Add('  CONTROL INCONCLUSIVE: the control endpoint state could not be read (enumerator failed), so causal isolation is weaker this run; the registry-clean evidence below still holds.')
    }
    if ($registryClean) {
        $verdicts.Add('  Registry is CLEAN after uninstall (original driver APO GUIDs restored, no EQ GUID dangling). The live loss is therefore the stale endpoint-graph bug: ApoRegistration::uninstall cycled only AudioSrv, never AudioEndpointBuilder.')
    }
    else {
        $verdicts.Add('  WARNING: registry is NOT clean after uninstall (an EQ APO GUID is still named or originals were not restored). The live loss may be compounded by a dangling-reference bug; see Diagnose-AudioUninstall.ps1.')
    }
}
else {
    $verdicts.Add("(live) NOT REPRODUCED: the Scream endpoint stayed ACTIVE after the AudioSrv-only uninstall (no reboot, after-state=$afterVeloState). Either the bug did not reproduce on this runner or Scream behaves differently here.")
}

if ($Mode -eq 'reproduce' -and $reproduced) {
    if ($null -eq $afterAeb) {
        $verdicts.Add('(live) FIX NOT EVALUATED: no post-AudioEndpointBuilder-restart snapshot (70-after-aeb) found.')
    }
    elseif ($fixValidated) {
        $verdicts.Add('(live) FIX VALIDATED: after Restart-Service -Force AudioEndpointBuilder the Scream endpoint returned to ACTIVE, WITHOUT a reboot. The fix candidate restores the endpoint live.')
    }
    elseif ($afterAebState -eq 'Unknown') {
        $verdicts.Add('(live) FIX NOT EVALUATED: the endpoint state could not be read after the AudioEndpointBuilder restart (COM enumerator failed). Inspect the 70-after-aeb snapshot.')
    }
    else {
        $verdicts.Add("(live) FIX NOT VALIDATED: the Scream endpoint stayed INACTIVE/missing even after the AudioEndpointBuilder restart (after-state=$afterAebState). The fix candidate is insufficient on this runner.")
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
if ($inconclusive) {
    Write-Host 'RESULT: INCONCLUSIVE - the live experiment could not be evaluated (exit 1). Inspect the snapshots.'
    exit 1
}
if ($Mode -eq 'expect-fixed') {
    if ($afterVeloState -eq 'Active' -and $registryClean) {
        Write-Host 'RESULT: (expect-fixed) PASS - the endpoint survived the real uninstall with audio in use AND the registry is clean; the shipped fixes work (exit 0).'
        exit 0
    }
    if ($afterVeloState -ne 'Active') {
        Write-Host 'RESULT: (expect-fixed) FAIL - the endpoint went inactive after the uninstall; the fix is absent or ineffective in this build (exit 2). Inspect the snapshots.'
    }
    else {
        Write-Host 'RESULT: (expect-fixed) FAIL - the uninstall left an EQ APO GUID dangling in FxProperties (26100+ NOKEY deleteKey regression, issue #189) (exit 2). Inspect the snapshots.'
    }
    exit 2
}
# reproduce mode
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
