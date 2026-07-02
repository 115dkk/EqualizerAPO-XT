<#
.SYNOPSIS
    Diffs the MMDevices/Audio registry snapshots produced by the
    audio-uninstall-repro workflow and prints a verdict that classifies the
    uninstall outcome into one of five classes.

.DESCRIPTION
    This is a forensic script for the "uninstalling EqualizerAPO-XT wiped all
    audio devices" bug report. It does NOT modify the system. It only reads the
    snapshot files (exported earlier in the job with `reg export` and a custom
    enumeration) and reasons about what changed for the seeded test endpoint.

    The snapshots are taken at named phases by the workflow:
      00-baseline    : runner state before anything
      10-seeded      : after the fake render endpoint is created
      20-installed   : after the Velopack *-Setup.exe ran (--silent)
      30-apo-applied : after the per-device APO install was replicated on the seed
      40-after-devsel: after DeviceSelector.exe /u
      50-after-velo  : after the full Velopack app uninstall
      60-reinstalled : after a second silent install

    Each phase has two files in $SnapshotDir:
      <phase>-mmdevices.reg   : `reg export` of the whole MMDevices\Audio tree
      <phase>-endpoint.json   : structured facts about the seeded endpoint
                                (key present? FxProperties present? which APO
                                GUID values are set, and to what)

    DIAGNOSIS CLASSES (see also the workflow comments):
      (A) ENDPOINT KEY DELETED   - the Render\{guid} key itself is gone after
                                   uninstall. This is the reported "device
                                   disappeared" failure at the registry level.
                                   FAIL.
      (B) FXPROPERTIES DELETED    - the endpoint key survives but its
                                   FxProperties subkey was deleted (effects
                                   removed, device still enumerable). This is
                                   the documented APOGUID_NOKEY branch behaviour
                                   and is, on its own, NOT the reported bug.
                                   WARN.
      (C) DANGLING APO REFERENCE  - the endpoint's FxProperties still point at
                                   an EqualizerAPO APO GUID (pre/post-mix) while
                                   the COM server is unregistered, OR the
                                   original driver APO GUIDs were not restored.
                                   audiodg cannot instantiate the chain, so
                                   Windows can mark the endpoint unusable / make
                                   it vanish from the UI. This is the most likely
                                   real-world cause of the report. FAIL.
      (D) FOLDER OVER-DELETION    - sibling endpoints or unrelated MMDevices
                                   subtrees disappeared (uninstall deleted more
                                   than the one device it touched). FAIL.
      (E) CLEAN                   - the original driver APO GUIDs were restored
                                   (or FxProperties cleanly removed in the
                                   no-key case it created), the endpoint key is
                                   intact, and no EQ APO GUID is left dangling.
                                   PASS.

    Exit code: non-zero when (A), (C) or (D) are detected (bug reproduced),
    zero otherwise. (B) is reported but does not fail the job, because deleting
    a FxProperties key that EqualizerAPO itself created is the intended restore
    behaviour and does not remove the device.

.PARAMETER SnapshotDir
    Directory containing the *-endpoint.json and *-mmdevices.reg snapshots.

.PARAMETER TestEndpointGuid
    The {guid} of the seeded render endpoint (without the {0.0.0.00000000}.
    prefix), matching what the workflow created under Render.

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
    [string]$TestEndpointGuid,

    [Parameter(Mandatory = $true)]
    [string]$PreMixGuid,

    [Parameter(Mandatory = $true)]
    [string]$PostMixGuid
)

$ErrorActionPreference = 'Stop'

function Read-Phase {
    param([string]$Phase)
    $jsonPath = Join-Path $SnapshotDir "$Phase-endpoint.json"
    if (-not (Test-Path $jsonPath)) {
        return $null
    }
    return Get-Content -Raw -Path $jsonPath | ConvertFrom-Json
}

function Format-ApoValues {
    param($Snapshot)
    if ($null -eq $Snapshot -or $null -eq $Snapshot.fxValues) {
        return '(none)'
    }
    $pairs = @()
    foreach ($prop in $Snapshot.fxValues.PSObject.Properties) {
        $pairs += "$($prop.Name) = $($prop.Value)"
    }
    if ($pairs.Count -eq 0) {
        return '(empty)'
    }
    return ($pairs -join '; ')
}

# Normalise GUIDs for case-insensitive comparison.
$preMix = $PreMixGuid.Trim().ToUpperInvariant()
$postMix = $PostMixGuid.Trim().ToUpperInvariant()

function Test-PointsAtEqApo {
    param($Snapshot)
    if ($null -eq $Snapshot -or $null -eq $Snapshot.fxValues) {
        return $false
    }
    foreach ($prop in $Snapshot.fxValues.PSObject.Properties) {
        $val = [string]$prop.Value
        if ([string]::IsNullOrWhiteSpace($val)) { continue }
        $norm = $val.Trim().ToUpperInvariant()
        if ($norm -eq $preMix -or $norm -eq $postMix) {
            return $true
        }
    }
    return $false
}

$baseline = Read-Phase '00-baseline'
$seeded = Read-Phase '10-seeded'
$applied = Read-Phase '30-apo-applied'
$afterDevSel = Read-Phase '40-after-devsel'
$afterVelo = Read-Phase '50-after-velo'

Write-Host '====================================================================='
Write-Host ' EqualizerAPO-XT  -  Audio uninstall reproduction diagnosis'
Write-Host '====================================================================='
Write-Host "Seeded endpoint GUID : $TestEndpointGuid"
Write-Host "EQ pre-mix  APO GUID : $PreMixGuid"
Write-Host "EQ post-mix APO GUID : $PostMixGuid"
Write-Host ''

foreach ($entry in @(
        @{ Label = 'seed (10)'; Snap = $seeded },
        @{ Label = 'apo applied (30)'; Snap = $applied },
        @{ Label = 'after DeviceSelector /u (40)'; Snap = $afterDevSel },
        @{ Label = 'after Velopack uninstall (50)'; Snap = $afterVelo })) {
    $s = $entry.Snap
    if ($null -eq $s) {
        Write-Host ("{0,-32}: <no snapshot>" -f $entry.Label)
        continue
    }
    Write-Host ("{0,-32}: keyPresent={1} fxPresent={2} apo=[{3}]" -f `
            $entry.Label, $s.endpointKeyPresent, $s.fxPropertiesPresent, (Format-ApoValues $s))
}
Write-Host ''

# The "final" post-uninstall state is whatever the latest available
# post-uninstall snapshot is (Velopack uninstall runs last; fall back to the
# DeviceSelector /u snapshot if the Velopack phase is missing).
$final = if ($null -ne $afterVelo) { $afterVelo } else { $afterDevSel }
if ($null -eq $final) {
    Write-Error 'No post-uninstall snapshot found; cannot diagnose.'
    exit 2
}

$verdicts = New-Object System.Collections.Generic.List[string]
$fail = $false

# --- (A) endpoint key deleted -------------------------------------------------
if ($final.endpointKeyPresent -eq $false) {
    $verdicts.Add('(A) ENDPOINT KEY DELETED: the seeded Render\{guid} key is gone after uninstall. This is the reported device-loss bug at the registry level.')
    $fail = $true
}

# --- (D) folder over-deletion -------------------------------------------------
# siblingRenderCount is the number of Render subkeys other than the seed. If it
# dropped between seed and final, uninstall removed unrelated devices.
if ($null -ne $seeded -and $null -ne $final -and
    $null -ne $seeded.siblingRenderCount -and $null -ne $final.siblingRenderCount) {
    if ([int]$final.siblingRenderCount -lt [int]$seeded.siblingRenderCount) {
        $verdicts.Add(("(D) FOLDER OVER-DELETION: sibling Render endpoints dropped from {0} to {1}; uninstall removed devices it never touched." -f `
                    $seeded.siblingRenderCount, $final.siblingRenderCount))
        $fail = $true
    }
}

# --- (C) dangling APO reference ----------------------------------------------
# After uninstall the COM server is unregistered. If FxProperties still names an
# EQ APO GUID, audiodg cannot load the chain -> the endpoint can be invalidated.
if ($final.endpointKeyPresent -ne $false) {
    if (Test-PointsAtEqApo $final) {
        $verdicts.Add('(C) DANGLING APO REFERENCE: FxProperties still points at an EqualizerAPO APO GUID after the COM server was unregistered. audiodg will fail to instantiate the endpoint.')
        $fail = $true
    }
    elseif ($null -ne $applied -and $applied.fxPropertiesPresent -and
        $final.fxPropertiesPresent -and ($null -ne $seeded) -and $seeded.fxPropertiesPresent) {
        # FxProperties existed before EqualizerAPO touched it, so uninstall
        # should have RESTORED the original driver APO GUIDs captured at seed.
        # Compare the final APO values against the seed's original APO values.
        $missing = @()
        foreach ($prop in $seeded.fxValues.PSObject.Properties) {
            $name = $prop.Name
            $want = [string]$prop.Value
            $haveProp = $final.fxValues.PSObject.Properties[$name]
            $have = if ($null -ne $haveProp) { [string]$haveProp.Value } else { $null }
            if ($want -ne $have) {
                $missing += "$name (was '$want', now '$have')"
            }
        }
        if ($missing.Count -gt 0) {
            $verdicts.Add(("(C) ORIGINAL APO GUIDS NOT RESTORED: " + ($missing -join ', ')))
            $fail = $true
        }
    }
}

# --- (B) FxProperties deleted (key intact) -----------------------------------
# This is informational. It is the intended behaviour when EqualizerAPO created
# the FxProperties key itself (APOGUID_NOKEY branch): on uninstall it deletes
# the whole FxProperties subkey. The device key survives, so the device is not
# lost. Only flag (B) when we did NOT already conclude a failing class.
if (-not $fail) {
    if ($final.endpointKeyPresent -ne $false -and $final.fxPropertiesPresent -eq $false) {
        $sxCreatedByEq = ($null -ne $seeded) -and ($seeded.fxPropertiesPresent -eq $false)
        if ($sxCreatedByEq) {
            $verdicts.Add('(B) FXPROPERTIES DELETED (intended): EqualizerAPO created the FxProperties key (it did not exist at seed) and removed it on uninstall. Device key intact; device not lost.')
        }
        else {
            $verdicts.Add('(B) FXPROPERTIES DELETED (unexpected): FxProperties existed at seed but was removed entirely instead of restoring the original APO values. Effects gone; device should still enumerate.')
        }
    }
}

# --- (E) clean ----------------------------------------------------------------
if (-not $fail -and $verdicts.Count -eq 0) {
    $verdicts.Add('(E) CLEAN: endpoint key intact, no EqualizerAPO APO GUID dangling, original driver APO GUIDs restored. No device loss reproduced.')
}

Write-Host '--------------------------------------------------------------------'
Write-Host ' VERDICT'
Write-Host '--------------------------------------------------------------------'
foreach ($v in $verdicts) {
    Write-Host " - $v"
}
Write-Host ''

if ($fail) {
    Write-Host 'RESULT: BUG REPRODUCED (failing class A/C/D detected).'
    exit 1
}
else {
    Write-Host 'RESULT: no device-loss bug reproduced on the seeded endpoint.'
    exit 0
}
