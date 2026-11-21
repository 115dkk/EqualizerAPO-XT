<# Runner-only cable setup and explicit endpoint format control. #>
param(
    [Parameter(Mandatory)] [ValidateSet('Install', 'Snapshot', 'SetFormat')] [string] $Mode,
    [Parameter(Mandatory)] [string] $Work,
    [Parameter(Mandatory)] [string] $OutputJson,
    [string] $EndpointId,
    [int] $Channels = 2,
    [int] $Rate = 48000
)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows') { throw 'Runner only.' }
if ($Mode -eq 'Install') {
    $directory = Join-Path $Work 'download-vbcable'
    New-Item -ItemType Directory -Path $directory | Out-Null
    $zip = Join-Path $directory 'cable.zip'
    Invoke-WebRequest -Uri 'https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack43.zip' -OutFile $zip -TimeoutSec 180
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne '66fd0a4d9f4896ff41632b7e3d53892c085c4561f53e8ae8d0f0bc10eedd1cdd') { throw 'VB-CABLE hash differs from pin.' }
    $unpacked = Join-Path $directory 'unpacked'
    Expand-Archive -LiteralPath $zip -DestinationPath $unpacked
    $catalog = Get-ChildItem -LiteralPath $unpacked -Recurse -Filter 'vbaudio_cable64_win7.cat' | Select-Object -First 1
    if (-not $catalog) { throw 'Driver catalog missing.' }
    $signature = Get-AuthenticodeSignature -LiteralPath $catalog.FullName
    if (-not $signature.SignerCertificate) { throw 'Driver signer certificate missing.' }
    $certificate = Join-Path $directory 'publisher.cer'
    Export-Certificate -Cert $signature.SignerCertificate -FilePath $certificate | Out-Null
    Import-Certificate -FilePath $certificate -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
    foreach ($service in @('AudioEndpointBuilder', 'AudioSrv')) {
        Set-Service -Name $service -StartupType Automatic
        Start-Service -Name $service
    }
    foreach ($root in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone', 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone')) {
        foreach ($key in @($root, "$root\NonPackaged")) {
            New-Item -Path $key -Force | Out-Null
            Set-ItemProperty -Path $key -Name Value -Value 'Allow' -Type String
        }
    }
    New-Item -Path 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\AppPrivacy' -Force | Out-Null
    Set-ItemProperty -Path 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\AppPrivacy' -Name LetAppsAccessMicrophone -Value 1 -Type DWord
    $setup = Get-ChildItem -LiteralPath $unpacked -Recurse -Filter 'VBCABLE_Setup_x64.exe' | Select-Object -First 1
    if (-not $setup) { throw 'Driver setup missing.' }
    $process = Start-Process -FilePath $setup.FullName -ArgumentList @('-i', '-h') -PassThru -WorkingDirectory $setup.DirectoryName
    if (-not $process.WaitForExit(240000)) { $process.Kill($true); throw 'Driver installer timed out.' }
    $setupExit = $process.ExitCode
    Restart-Service -Name AudioEndpointBuilder -Force
    Start-Service -Name AudioSrv
    Start-Sleep -Seconds 8
}

# The undocumented policy interface is used only to set this explicitly named
# disposable endpoint's format. We never call SetDefaultEndpoint. Its vtable
# through SetDeviceFormat matches IPolicyConfig on Windows 7 and later.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class EndpointEnumeratorClass {}
[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IEndpointEnumerator {
 [PreserveSig] int EnumAudioEndpoints(int flow, uint mask, out IntPtr collection);
 [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IntPtr device);
 [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IEndpoint device);
}
[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IEndpoint {
 [PreserveSig] int Activate(ref Guid iid, uint context, IntPtr parameters, [MarshalAs(UnmanagedType.IUnknown)] out object result);
}
[ComImport, Guid("1CB9AD4C-DBFA-4C32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IEndpointAudioClient {
 [PreserveSig] int Initialize(int mode, uint flags, long buffer, long period, IntPtr format, IntPtr session);
 [PreserveSig] int GetBufferSize(out uint frames);
 [PreserveSig] int GetStreamLatency(out long latency);
 [PreserveSig] int GetCurrentPadding(out uint frames);
 [PreserveSig] int IsFormatSupported(int mode, IntPtr format, out IntPtr closest);
 [PreserveSig] int GetMixFormat(out IntPtr format);
}
[ComImport, Guid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9")] class EndpointPolicyClass {}
[ComImport, Guid("F8679F50-850A-41CF-9C72-430F290290C8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IEndpointPolicy {
 [PreserveSig] int GetMixFormat([MarshalAs(UnmanagedType.LPWStr)] string id, out IntPtr format);
 [PreserveSig] int GetDeviceFormat([MarshalAs(UnmanagedType.LPWStr)] string id, [MarshalAs(UnmanagedType.Bool)] bool defaults, out IntPtr format);
 [PreserveSig] int ResetDeviceFormat([MarshalAs(UnmanagedType.LPWStr)] string id);
 [PreserveSig] int SetDeviceFormat([MarshalAs(UnmanagedType.LPWStr)] string id, IntPtr endpointFormat, IntPtr mixFormat);
}
public class EndpointFormat {
 public int channels; public int rate; public int bits; public int tag; public uint channel_mask;
}
public static class BenchmarkEndpoint {
 public static EndpointFormat Mix(string id) {
  var enumerator = (IEndpointEnumerator)new EndpointEnumeratorClass();
  IEndpoint device = null; object activated = null; IntPtr format = IntPtr.Zero;
  try {
   Marshal.ThrowExceptionForHR(enumerator.GetDevice(id, out device));
   var iid = new Guid("1CB9AD4C-DBFA-4C32-B178-C2F568A703B2");
   Marshal.ThrowExceptionForHR(device.Activate(ref iid, 23, IntPtr.Zero, out activated));
   Marshal.ThrowExceptionForHR(((IEndpointAudioClient)activated).GetMixFormat(out format));
   int tag = (ushort)Marshal.ReadInt16(format, 0);
   return new EndpointFormat { tag=tag, channels=(ushort)Marshal.ReadInt16(format,2), rate=Marshal.ReadInt32(format,4),
     bits=(ushort)Marshal.ReadInt16(format,14), channel_mask=tag==65534 ? (uint)Marshal.ReadInt32(format,20) : 0 };
  } finally {
   if(format!=IntPtr.Zero) Marshal.FreeCoTaskMem(format);
   if(activated!=null) Marshal.ReleaseComObject(activated);
   if(device!=null) Marshal.ReleaseComObject(device);
   Marshal.ReleaseComObject(enumerator);
  }
 }
 static IntPtr Format(int channels, int rate, bool floating) {
  int bits = floating ? 32 : 16;
  byte[] bytes = new byte[40];
  Action<int,byte[]> put = (offset,data) => Array.Copy(data,0,bytes,offset,data.Length);
  put(0,BitConverter.GetBytes((ushort)65534)); put(2,BitConverter.GetBytes((ushort)channels));
  put(4,BitConverter.GetBytes(rate)); put(8,BitConverter.GetBytes(rate*channels*bits/8));
  put(12,BitConverter.GetBytes((ushort)(channels*bits/8))); put(14,BitConverter.GetBytes((ushort)bits));
  put(16,BitConverter.GetBytes((ushort)22)); put(18,BitConverter.GetBytes((ushort)bits));
  uint mask = channels==2 ? 3u : channels==12 ? 0x2d63fu : channels==16 ? 0x3ff3fu : 0u;
  put(20,BitConverter.GetBytes(mask)); put(24,new Guid(floating ? "00000003-0000-0010-8000-00aa00389b71" : "00000001-0000-0010-8000-00aa00389b71").ToByteArray());
  IntPtr pointer=Marshal.AllocHGlobal(40); Marshal.Copy(bytes,0,pointer,40); return pointer;
 }
 public static int Set(string id, int channels, int rate) {
  var policy=(IEndpointPolicy)new EndpointPolicyClass();
  IntPtr device=Format(channels,rate,false), mix=Format(channels,rate,true);
  try { return policy.SetDeviceFormat(id,device,mix); }
  finally { Marshal.FreeHGlobal(device); Marshal.FreeHGlobal(mix); Marshal.ReleaseComObject(policy); }
 }
}
'@
$setResult = $null
if ($Mode -eq 'SetFormat') {
    if (-not $EndpointId) { throw 'An explicit endpoint ID is required.' }
    $setResult = [BenchmarkEndpoint]::Set($EndpointId, $Channels, $Rate)
}
$endpoints = @()
foreach ($flow in @('Render', 'Capture')) {
    $root = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\$flow"
    if (-not (Test-Path -LiteralPath $root)) { continue }
    foreach ($key in Get-ChildItem -LiteralPath $root) {
        $state = (Get-ItemProperty -LiteralPath $key.PSPath -Name DeviceState -ErrorAction SilentlyContinue).DeviceState
        if ($state -ne 1) { continue }
        $props = Get-ItemProperty -LiteralPath "$($key.PSPath)\Properties"
        $property = $props.PSObject.Properties['{a45c254e-df1c-4efd-8020-67d146a850e0},2']
        if (-not $property -or $property.Value -notlike 'CABLE*') { continue }
        $prefix = if ($flow -eq 'Render') { '{0.0.0.00000000}.' } else { '{0.0.1.00000000}.' }
        $id = $prefix + $key.PSChildName
        $row = [ordered]@{ name = [string]$property.Value; flow = $flow; id = $id; guid = $key.PSChildName; format = $null; error_hresult = $null }
        try { $row.format = [BenchmarkEndpoint]::Mix($id) }
        catch { $row.error_hresult = $_.Exception.HResult }
        $endpoints += $row
    }
}
$result = [ordered]@{ mode = $Mode; set_hresult = $setResult; endpoints = $endpoints }
if ($Mode -eq 'Install') { $result.sha256 = $hash; $result.installer_exit = $setupExit }
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputJson
