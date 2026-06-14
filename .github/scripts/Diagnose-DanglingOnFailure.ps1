<#
.SYNOPSIS
    Diagnoses the "dangling APO reference after a per-device uninstall failure"
    bug (class C) for the dangling-on-per-device-failure job of the
    audio-uninstall-repro workflow.

.DESCRIPTION
    This script is the verdict for the SECOND job in
    .github/workflows/audio-uninstall-repro.yml. It is forensic only: it reads
    the snapshot JSON files the job wrote and reasons about what the REAL
    Velopack app uninstall (Update.exe --uninstall -> --veloapp-uninstall hook
    -> ApoRegistration::uninstall + DllUnregisterServer) left behind for two
    seeded render endpoints.

    THE BUG ("(b)")  -- source references:
      ApoRegistration::uninstall (helpers/ApoRegistration.cpp:250-313) loops
      every device and calls apoInfo->uninstall() for installed ones inside a
      try/catch that LOGS and CONTINUES on RegistryException/DeviceException
      (lines 260-275), then UNCONDITIONALLY unregisters the COM server
      (registerComServer(dllPath, true) = DllUnregisterServer, line 281).
      So if even ONE device's per-device de-register throws, that device keeps
      the EqualizerAPO APO GUID in its FxProperties while the COM server is
      removed -> audiodg can no longer instantiate the APO -> that endpoint
      dies.

      The realistic forced failure: DeviceAPOInfo::uninstall()
      (devices/DeviceAPOInfo.Uninstall.cpp:66-102), in the
      originalApoGuids[0]==APOGUID_NOKEY branch (the "!KEY" sentinel,
      DeviceAPOInfo.h:28), calls RegistryHelper::deleteKey(keyPath\FxProperties)
      (Uninstall.cpp:74-77). deleteKey uses RegDeleteKeyExW
      (helpers/RegistryHelper.cpp:291-299), which CANNOT delete a key that still
      has subkeys, so if FxProperties has a stray processing-mode subkey the
      delete throws RegistryException. uninstall() has NO takeOwnership /
      makeWritable fallback (that only exists in install(),
      DeviceAPOInfo.Install.cpp:88-92), so the throw propagates, is caught by
      ApoRegistration::uninstall, the loop continues, and COM is unregistered
      anyway. The endpoint is left dangling.

    THE TWO SEEDED ENDPOINTS:
      Device A (clean)    : FxProperties pre-existed with original driver APO
                            GUIDs; saved originals in Child APOs\A are the real
                            GUIDs, so uninstall takes the restore branch
                            (Uninstall.cpp:78-91) and A is cleanly restored.
      Device B (poisoned) : the NOKEY install state -- EqualizerAPO created
                            FxProperties, so the saved originals in Child APOs\B
                            are all the "!KEY" sentinel, making uninstall take
                            the deleteKey branch. A stray subkey under
                            B\FxProperties makes RegDeleteKeyExW FAIL -> B is
                            left pointing at the EQ APO GUID.

    VERDICT CONVENTION (DELIBERATELY INVERTED vs Diagnose-AudioUninstall.ps1):
      This job exists to PROVE the bug, so a GREEN run means the bug reproduced.
        - Prints "<b> REPRODUCED" and EXITS 0 when, after the Velopack uninstall:
            * B still dangles (B endpoint key present, B FxProperties present and
              still naming an EqualizerAPO pre/post-mix APO GUID), AND
            * the EQ COM server is unregistered (the InprocServer32 under
              HKCR\CLSID\{premix}/{postmix} is gone).
          That pair is exactly the dangling state audiodg cannot satisfy.
        - Prints "<b> NOT REPRODUCED" and EXITS 1 otherwise (e.g. B was cleanly
          restored, or B dangles but COM is somehow still registered so the
          reference is not actually dangling).
      NOTE FOR READERS: do not confuse this with the clean-path job. There a
      green run means "no bug". Here a green run means "bug reproduced". The
      job step name and this banner make the inversion explicit.

    LIMITATION (same as the clean-path job): a headless CI runner has no
    audiodg.exe loading a real APO chain for the seeded endpoint, so this proves
    the registry is LEFT in the dangling state (the necessary precondition for
    the reported device loss), not the live kernel disappearance. Confirming the
    full live failure needs hardware with a real driver.

.PARAMETER SnapshotDir
    Directory containing the *-dangling.json snapshots written by the job.

.PARAMETER EndpointAGuid
    The {guid} of the clean seeded render endpoint (Device A).

.PARAMETER EndpointBGuid
    The {guid} of the poisoned seeded render endpoint (Device B).

.PARAMETER PreMixGuid
    EqualizerAPO pre-mix APO CLSID string, e.g. {EACD2258-FCAC-4FF4-B36D-419E924A6D79}.

.PARAMETER PostMixGuid
    EqualizerAPO post-mix APO CLSID string, e.g. {EC1CC9CE-FAED-4822-828A-82A81A6F018F}.

.PARAMETER OrigLfxGuid
    Original driver LFX APO GUID seeded for Device A (restore target).

.PARAMETER OrigGfxGuid
    Original driver GFX APO GUID seeded for Device A (restore target).
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SnapshotDir,

    [Parameter(Mandatory = $true)]
    [string]$EndpointAGuid,

    [Parameter(Mandatory = $true)]
    [string]$EndpointBGuid,

    [Parameter(Mandatory = $true)]
    [string]$PreMixGuid,

    [Parameter(Mandatory = $true)]
    [string]$PostMixGuid,

    [Parameter(Mandatory = $true)]
    [string]$OrigLfxGuid,

    [Parameter(Mandatory = $true)]
    [string]$OrigGfxGuid
)

$ErrorActionPreference = 'Stop'

function Read-Phase {
    param([string]$Phase)
    $jsonPath = Join-Path $SnapshotDir "$Phase-dangling.json"
    if (-not (Test-Path $jsonPath)) {
        return $null
    }
    return Get-Content -Raw -Path $jsonPath | ConvertFrom-Json
}

# Normalise GUIDs for case-insensitive comparison.
$preMix = $PreMixGuid.Trim().ToUpperInvariant()
$postMix = $PostMixGuid.Trim().ToUpperInvariant()
$origLfx = $OrigLfxGuid.Trim().ToUpperInvariant()
$origGfx = $OrigGfxGuid.Trim().ToUpperInvariant()

function Format-ApoValues {
    param($Endpoint)
    if ($null -eq $Endpoint -or $null -eq $Endpoint.fxValues) {
        return '(none)'
    }
    $pairs = @()
    foreach ($prop in $Endpoint.fxValues.PSObject.Properties) {
        $pairs += "$($prop.Name) = $($prop.Value)"
    }
    if ($pairs.Count -eq 0) {
        return '(empty)'
    }
    return ($pairs -join '; ')
}

function Test-PointsAtEqApo {
    param($Endpoint)
    if ($null -eq $Endpoint -or $null -eq $Endpoint.fxValues) {
        return $false
    }
    foreach ($prop in $Endpoint.fxValues.PSObject.Properties) {
        $val = [string]$prop.Value
        if ([string]::IsNullOrWhiteSpace($val)) { continue }
        $norm = $val.Trim().ToUpperInvariant()
        if ($norm -eq $preMix -or $norm -eq $postMix) {
            return $true
        }
    }
    return $false
}

function Get-ApoValue {
    param($Endpoint, [string]$Name)
    if ($null -eq $Endpoint -or $null -eq $Endpoint.fxValues) { return $null }
    $p = $Endpoint.fxValues.PSObject.Properties[$Name]
    if ($null -eq $p) { return $null }
    return [string]$p.Value
}

$applied = Read-Phase '30-apo-applied'
$installed = Read-Phase '20-installed'
$afterVelo = Read-Phase '50-after-velo'

Write-Host '====================================================================='
Write-Host ' EqualizerAPO-XT  -  Dangling-on-per-device-failure diagnosis  (b)'
Write-Host '====================================================================='
Write-Host "Endpoint A (clean)    : $EndpointAGuid"
Write-Host "Endpoint B (poisoned) : $EndpointBGuid"
Write-Host "EQ pre-mix  APO GUID  : $PreMixGuid"
Write-Host "EQ post-mix APO GUID  : $PostMixGuid"
Write-Host ''
Write-Host 'VERDICT CONVENTION (INVERTED vs the clean-path job):'
Write-Host '  exit 0 / GREEN  = (b) REPRODUCED  (B dangles AND COM unregistered)'
Write-Host '  exit 1 / RED    = (b) NOT REPRODUCED'
Write-Host ''

foreach ($entry in @(
        @{ Label = 'after silent install (20)'; Snap = $installed },
        @{ Label = 'after APO applied A+B (30)'; Snap = $applied },
        @{ Label = 'after Velopack uninstall (50)'; Snap = $afterVelo })) {
    $s = $entry.Snap
    if ($null -eq $s) {
        Write-Host ("{0,-30}: <no snapshot>" -f $entry.Label)
        continue
    }
    $a = $s.endpointA
    $b = $s.endpointB
    Write-Host ("{0,-30}:" -f $entry.Label)
    Write-Host ("    A keyPresent={0} fxPresent={1} apo=[{2}]" -f $a.keyPresent, $a.fxPresent, (Format-ApoValues $a))
    Write-Host ("    B keyPresent={0} fxPresent={1} fxSubkeys={2} apo=[{3}]" -f $b.keyPresent, $b.fxPresent, $b.fxSubkeyCount, (Format-ApoValues $b))
    Write-Host ("    COM premix InprocServer32={0} postmix InprocServer32={1} (clsid keys: pre={2} post={3})" -f `
            $s.comPreMixInproc, $s.comPostMixInproc, $s.comPreMixClsidPresent, $s.comPostMixClsidPresent)
}
Write-Host ''

if ($null -eq $afterVelo) {
    Write-Error 'No post-Velopack-uninstall snapshot (50) found; cannot diagnose.'
    exit 2
}

$a = $afterVelo.endpointA
$b = $afterVelo.endpointB

# --- COM-was-registered sanity (evidence the unregister had something to do) --
# Best-effort: if the install snapshot shows the InprocServer32 was present, we
# can later assert it became absent. We do not hard-fail if 20 is missing.
$comWasRegistered = $false
if ($null -ne $installed) {
    $comWasRegistered = ($installed.comPreMixInproc -eq $true) -or ($installed.comPostMixInproc -eq $true)
}

# --- Half 1: is B dangling? ---------------------------------------------------
$bKeyPresent = ($b.keyPresent -eq $true)
$bFxPresent = ($b.fxPresent -eq $true)
$bPointsAtEq = Test-PointsAtEqApo $b
$bDangles = $bKeyPresent -and $bFxPresent -and $bPointsAtEq

# --- Half 2: is the EQ COM server unregistered? -------------------------------
# Dangling requires the COM CLSID's InprocServer32 to be gone for BOTH APOs (the
# audio engine cannot instantiate either pre- or post-mix APO).
$comGone = ($afterVelo.comPreMixInproc -ne $true) -and ($afterVelo.comPostMixInproc -ne $true)

# --- Corroboration: was A cleanly restored? -----------------------------------
$aKeyPresent = ($a.keyPresent -eq $true)
$aFxPresent = ($a.fxPresent -eq $true)
$aPointsAtEq = Test-PointsAtEqApo $a
$aLfx = (Get-ApoValue $a $applied.lfxValueName)
$aGfx = (Get-ApoValue $a $applied.gfxValueName)
$aLfxNorm = if ($null -ne $aLfx) { $aLfx.Trim().ToUpperInvariant() } else { $null }
$aGfxNorm = if ($null -ne $aGfx) { $aGfx.Trim().ToUpperInvariant() } else { $null }
$aRestored = $aKeyPresent -and $aFxPresent -and (-not $aPointsAtEq) -and `
    ($aLfxNorm -eq $origLfx) -and ($aGfxNorm -eq $origGfx)

Write-Host '--------------------------------------------------------------------'
Write-Host ' EVIDENCE'
Write-Host '--------------------------------------------------------------------'
Write-Host (" B endpoint key present      : {0}" -f $bKeyPresent)
Write-Host (" B FxProperties present      : {0}" -f $bFxPresent)
Write-Host (" B still names an EQ APO GUID: {0}" -f $bPointsAtEq)
Write-Host (" B fx subkeys (delete-block) : {0}" -f $b.fxSubkeyCount)
Write-Host (" => B dangles                : {0}" -f $bDangles)
Write-Host ''
Write-Host (" COM was registered post-install (20): {0}" -f $comWasRegistered)
Write-Host (" EQ premix  InprocServer32 gone      : {0}" -f ($afterVelo.comPreMixInproc -ne $true))
Write-Host (" EQ postmix InprocServer32 gone      : {0}" -f ($afterVelo.comPostMixInproc -ne $true))
Write-Host (" => COM unregistered                 : {0}" -f $comGone)
Write-Host ''
Write-Host (" A endpoint key present      : {0}" -f $aKeyPresent)
Write-Host (" A restored to originals     : {0} (LFX now '{1}', want '{2}'; GFX now '{3}', want '{4}')" -f `
        $aRestored, $aLfx, $OrigLfxGuid, $aGfx, $OrigGfxGuid)
Write-Host ''

$reproduced = $bDangles -and $comGone

Write-Host '--------------------------------------------------------------------'
Write-Host ' VERDICT'
Write-Host '--------------------------------------------------------------------'
if ($reproduced) {
    Write-Host ' (b) REPRODUCED'
    Write-Host '   The poisoned device B was left pointing at an EqualizerAPO APO'
    Write-Host '   GUID in its FxProperties (its NOKEY deleteKey threw because of the'
    Write-Host '   stray subkey), the per-device exception was swallowed, and the EQ'
    Write-Host '   COM server was unregistered anyway -> DANGLING reference (class C).'
    Write-Host '   audiodg cannot instantiate the APO for B; on real hardware the'
    Write-Host '   endpoint would be invalidated.'
    if ($aRestored) {
        Write-Host '   Corroboration: the clean device A WAS restored to its original'
        Write-Host '   driver APO GUIDs, so the damage is isolated to the poisoned device.'
    }
    else {
        Write-Host '   NOTE: device A was not cleanly restored either; inspect the 50 snapshot.'
    }
    Write-Host ''
    Write-Host 'RESULT: GREEN means the bug reproduced (inverted convention).'
    exit 0
}
else {
    Write-Host ' (b) NOT REPRODUCED'
    if (-not $bDangles) {
        Write-Host '   Device B did NOT end up dangling: either its FxProperties was'
        Write-Host '   removed/restored, the endpoint key is gone, or it no longer names'
        Write-Host '   an EQ APO GUID. The deleteKey-subkey poison may not have fired.'
    }
    if (-not $comGone) {
        Write-Host '   The EQ COM server still appears registered (InprocServer32 present),'
        Write-Host '   so even a lingering EQ GUID would not be a dangling reference.'
        Write-Host '   Check that the Velopack --veloapp-uninstall hook ran and that the'
        Write-Host '   DLL was loadable when DllUnregisterServer was called.'
    }
    Write-Host ''
    Write-Host 'RESULT: RED means the bug did not reproduce on the seeded endpoints.'
    exit 1
}
