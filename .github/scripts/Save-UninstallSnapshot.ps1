# Snapshot helper for .github/workflows/audio-uninstall-repro.yml: one
# phase's MMDevices export and endpoint facts. Audit #422 TD-48 moved it
# here from a here-string the workflow wrote at run time.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Phase,
    [Parameter(Mandatory=$true)][string]$SnapshotDir,
    [Parameter(Mandatory=$true)][string]$TestEndpointGuid,
    [Parameter(Mandatory=$true)][string]$MMDevicesRoot
)
$ErrorActionPreference = "Stop"

# 1) Full MMDevices\Audio tree as a .reg export (forensic record).
$regFile = Join-Path $SnapshotDir "$Phase-mmdevices.reg"
reg export $MMDevicesRoot $regFile /y | Out-Null

# 2) Structured facts about the seeded endpoint + sibling counts.
$renderPath = "$MMDevicesRoot\Render"
$epKeyPS  = "Registry::$renderPath\$TestEndpointGuid"
$fxKeyPS  = "$epKeyPS\FxProperties"

$endpointKeyPresent  = Test-Path $epKeyPS
$fxPropertiesPresent = Test-Path $fxKeyPS

$fxValues = [ordered]@{}
if ($fxPropertiesPresent) {
    $item = Get-Item $fxKeyPS
    foreach ($name in $item.GetValueNames()) {
        # Only capture the five APO GUID value names we care about.
        if ($name -like '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},*') {
            $fxValues[$name] = [string](Get-ItemProperty -Path $fxKeyPS -Name $name).$name
        }
    }
}

# Count Render subkeys OTHER than the seed (over-deletion detector).
$siblingRenderCount = 0
if (Test-Path "Registry::$renderPath") {
    $siblingRenderCount = (Get-ChildItem "Registry::$renderPath" |
        Where-Object { $_.PSChildName -ne $TestEndpointGuid } | Measure-Object).Count
}

# Whether the EqualizerAPO Child APOs saved-state for the seed exists.
$childApoPresent = Test-Path "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO\Child APOs\$TestEndpointGuid"

# PnP endpoint enumeration (best effort; empty on headless runners).
$pnp = @()
try {
    $pnp = Get-PnpDevice -Class AudioEndpoint -ErrorAction Stop |
        Select-Object Status, FriendlyName, InstanceId
} catch { }

$obj = [ordered]@{
    phase                = $Phase
    endpointKeyPresent   = $endpointKeyPresent
    fxPropertiesPresent  = $fxPropertiesPresent
    fxValues             = $fxValues
    siblingRenderCount   = $siblingRenderCount
    childApoPresent      = $childApoPresent
    pnpAudioEndpoints    = $pnp
}
$obj | ConvertTo-Json -Depth 6 |
    Out-File -FilePath (Join-Path $SnapshotDir "$Phase-endpoint.json") -Encoding utf8
Write-Host "[$Phase] keyPresent=$endpointKeyPresent fxPresent=$fxPropertiesPresent siblings=$siblingRenderCount childApo=$childApoPresent"
