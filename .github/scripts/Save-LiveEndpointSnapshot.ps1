# Snapshot helper for .github/workflows/audio-live-repro.yml: one phase's
# live endpoint state. Audit #422 TD-48 moved it here from a here-string
# the workflow wrote at run time.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Phase,
    [Parameter(Mandatory=$true)][string]$SnapshotDir,
    [Parameter(Mandatory=$true)][string]$ScreamGuid,
    [Parameter(Mandatory=$true)][string]$MMDevicesRoot,
    [Parameter(Mandatory=$true)][string]$PreMixGuid,
    [Parameter(Mandatory=$true)][string]$PostMixGuid
)
$ErrorActionPreference = "Stop"

# 1) Full MMDevices\Audio tree as a .reg export (forensic record).
$regFile = Join-Path $SnapshotDir "$Phase-mmdevices.reg"
reg export $MMDevicesRoot $regFile /y | Out-Null

$renderPath = "$MMDevicesRoot\Render"
$epKeyPS = "Registry::$renderPath\$ScreamGuid"
$fxKeyPS = "$epKeyPS\FxProperties"

$endpointKeyPresent = Test-Path $epKeyPS
$fxPresent = Test-Path $fxKeyPS

# MMDevices DeviceState DWORD (registry view of activeness; 1 == ACTIVE).
$mmState = $null
if ($endpointKeyPresent) {
    $mmState = (Get-ItemProperty -Path $epKeyPS -Name DeviceState -ErrorAction SilentlyContinue).DeviceState
}

# FxProperties APO GUID values + whether any names an EQ APO GUID.
$preMix = $PreMixGuid.Trim().ToUpperInvariant()
$postMix = $PostMixGuid.Trim().ToUpperInvariant()
$fxValues = [ordered]@{}
$pointsAtEq = $false
if ($fxPresent) {
    $item = Get-Item $fxKeyPS
    foreach ($name in $item.GetValueNames()) {
        if ($name -like '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},*') {
            $v = [string](Get-ItemProperty -Path $fxKeyPS -Name $name)."$name"
            $fxValues[$name] = $v
            if (-not [string]::IsNullOrWhiteSpace($v)) {
                $n = $v.Trim().ToUpperInvariant()
                if ($n -eq $preMix -or $n -eq $postMix) { $pointsAtEq = $true }
            }
        }
    }
}
# "Registry clean" = endpoint present AND no EQ GUID dangling in FxProperties.
$registryClean = ($endpointKeyPresent -and -not $pointsAtEq)

# 2) LIVE endpoint state via IMMDeviceEnumerator (the kernel-graph view).
#    activeRenderCount = number of DEVICE_STATE_ACTIVE render endpoints.
#    screamEndpointState = IMMDevice::GetState for the Scream endpoint, or
#    "NOTFOUND" when it no longer enumerates.
Add-Type -Language CSharp -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class MMLive {
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumerator {}
    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator {
        int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection devices);
        int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice ep);
        int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice dev);
    }
    [Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceCollection { int GetCount(out int count); int Item(int n, out IMMDevice dev); }
    [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice {
        int Activate(ref Guid id, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o);
        int OpenPropertyStore(int access, out IntPtr store);
        int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        int GetState(out int state);
    }
    const int eRender = 0;
    const int DEVICE_STATE_ACTIVE = 0x1;
    const int DEVICE_STATEMASK_ALL = 0x0000000F;
    public static int ActiveRenderCount() {
        var e = (IMMDeviceEnumerator)(new MMDeviceEnumerator());
        IMMDeviceCollection col; e.EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, out col);
        int n; col.GetCount(out n); return n;
    }
    // Returns the GetState bitmask for the endpoint id, or -1 if not found.
    public static int EndpointState(string id) {
        var e = (IMMDeviceEnumerator)(new MMDeviceEnumerator());
        IMMDevice d;
        int hr = e.GetDevice(id, out d);
        if (hr != 0 || d == null) return -1;
        int st; if (d.GetState(out st) != 0) return -1; return st;
    }
}
"@

# Retry the enumerator until it is HEALTHY. Right after a WASAPI Stop()
# or a service restart the COM layer can throw transiently (the first
# run saw activeRender=-1 at one phase even though the same call worked
# seconds earlier). Treating that transient throw as "endpoint gone"
# was a false negative. So we poll ActiveRenderCount until it returns a
# non-negative count (enumerator healthy) and only THEN read the
# endpoint state, so a -1 from EndpointState genuinely means not-found.
$activeRenderCount = -1
$enumHealthy = $false
for ($attempt = 0; $attempt -lt 10; $attempt++) {
    try { $activeRenderCount = [MMLive]::ActiveRenderCount(); $enumHealthy = $true; break }
    catch { Start-Sleep -Milliseconds 1000 }
}

# IMMDevice ids for render endpoints are "{0.0.0.00000000}.<guid>"
# (DeviceAPOInfo.Uninstall.cpp:208). Map the GetState bitmask to a label.
$screamId = "{0.0.0.00000000}.$ScreamGuid"
$stateBits = -1
if ($enumHealthy) {
    try { $stateBits = [MMLive]::EndpointState($screamId) } catch { $stateBits = -1 }
}
$screamEndpointState =
    if (-not $enumHealthy) { 'ENUM_FAILED' }   # transient COM failure, NOT a device-loss signal
    else {
        switch ($stateBits) {
            1   { 'ACTIVE' }
            2   { 'DISABLED' }
            4   { 'NOTPRESENT' }
            8   { 'UNPLUGGED' }
            -1  { 'NOTFOUND' }
            default { "OTHER($stateBits)" }
        }
    }

# Win32_SoundDevice status strings (coarse OS-level view).
$win32 = @()
try {
    $win32 = Get-CimInstance Win32_SoundDevice -ErrorAction Stop |
        Select-Object Name, Status, StatusInfo
} catch { }

# audiodg.exe identity. audiodg (Windows Audio Device Graph Isolation)
# is spawned by AudioEndpointBuilder and hosts the APO chain. Its PID is
# the cleanest signal for "did this restart actually rebuild the graph":
# an AudioSrv-only restart leaves audiodg running (same PID) so APO
# FxProperties changes do NOT take effect, whereas an AudioEndpointBuilder
# restart recycles audiodg (new PID) so changes apply. (audiodg is a
# protected process; .Id is always readable, .StartTime may be denied.)
$audiodg = @()
foreach ($p in (Get-Process -Name audiodg -ErrorAction SilentlyContinue)) {
    $st = $null
    try { $st = $p.StartTime.ToString('o') } catch { }
    $audiodg += [ordered]@{ id = $p.Id; start = $st }
}
$audiodgIds = ($audiodg | ForEach-Object { $_.id }) -join ','

$obj = [ordered]@{
    phase                  = $Phase
    screamGuid             = $ScreamGuid
    endpointKeyPresent     = $endpointKeyPresent
    fxPropertiesPresent    = $fxPresent
    fxValues               = $fxValues
    screamFxPointsAtEq     = $pointsAtEq
    registryClean          = $registryClean
    screamDeviceStateMM    = $mmState
    activeRenderCount      = $activeRenderCount
    screamEndpointState    = $screamEndpointState
    audiodgProcs           = $audiodg
    audiodgIds             = $audiodgIds
    win32SoundDeviceStatus = $win32
}
$obj | ConvertTo-Json -Depth 6 |
    Out-File -FilePath (Join-Path $SnapshotDir "$Phase-live.json") -Encoding utf8
Write-Host "[$Phase] epState=$screamEndpointState mmState=$mmState activeRender=$activeRenderCount fxPointsAtEq=$pointsAtEq registryClean=$registryClean audiodg=[$audiodgIds]"
