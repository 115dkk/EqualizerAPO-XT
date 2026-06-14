<#
.SYNOPSIS
    Holds a continuous WASAPI shared-mode render stream (silence) open on a given
    audio endpoint for a fixed duration. Used by audio-live-repro.yml to keep
    audiodg actively loading the endpoint's APO chain WHILE the uninstall removes
    the EQ APO - the faithful "audio was in use during uninstall" scenario the
    headless 1-second stream-force did not cover.

.DESCRIPTION
    Run as a detached background process (Start-Process pwsh -File ...). It opens
    an IAudioClient on the endpoint id, gets an IAudioRenderClient, Start()s the
    stream, then writes AUDCLNT_BUFFERFLAGS_SILENT buffers in a loop until the
    duration elapses. The loop TOLERATES device-invalidation errors (it keeps the
    process alive and counts them) so the process reliably spans the whole
    uninstall window and remains killable by the workflow; an error count > 0 is
    itself a signal that the endpoint went away while the stream was live.

    The endpoint id is the "{0.0.0.00000000}.<guid>" form (the IMMDevice id).

.PARAMETER ScreamId
    The IMMDevice id of the render endpoint, e.g. "{0.0.0.00000000}.{<guid>}".

.PARAMETER Seconds
    How long to hold the stream. The workflow kills the process earlier once the
    post-uninstall snapshot is taken; this is just an upper bound.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ScreamId,
    [Parameter(Mandatory = $true)][int]$Seconds
)
$ErrorActionPreference = "Stop"

Add-Type -Language CSharp -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Threading;
public static class WasapiHold {
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumerator {}
    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator {
        int EnumAudioEndpoints(int f, int m, out IntPtr c);
        int GetDefaultAudioEndpoint(int f, int r, out IMMDevice ep);
        int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice dev);
    }
    [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice {
        int Activate(ref Guid id, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o);
        int OpenPropertyStore(int a, out IntPtr s);
        int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        int GetState(out int st);
    }
    [Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioClient {
        int Initialize(int share, int flags, long buf, long period, IntPtr fmt, IntPtr guid);
        int GetBufferSize(out uint frames);
        int GetStreamLatency(out long l);
        int GetCurrentPadding(out uint p);
        int IsFormatSupported(int share, IntPtr fmt, out IntPtr closest);
        int GetMixFormat(out IntPtr fmt);
        int GetDevicePeriod(out long def, out long min);
        int Start();
        int Stop();
        int Reset();
        int SetEventHandle(IntPtr h);
        int GetService(ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object svc);
    }
    [Guid("F294ACFC-3146-4483-A7BF-ADDCA7C260E2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioRenderClient {
        int GetBuffer(int numFrames, out IntPtr data);
        int ReleaseBuffer(int numFrames, int flags);
    }
    const int CLSCTX_ALL = 0x17;
    const int AUDCLNT_SHAREMODE_SHARED = 0;
    const int AUDCLNT_BUFFERFLAGS_SILENT = 0x2;
    static Guid IID_IAudioClient = new Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2");
    static Guid IID_IAudioRenderClient = new Guid("F294ACFC-3146-4483-A7BF-ADDCA7C260E2");
    public static string Run(string id, int seconds) {
        IMMDeviceEnumerator e = (IMMDeviceEnumerator)(new MMDeviceEnumerator());
        IMMDevice dev;
        int hr = e.GetDevice(id, out dev);
        if (hr != 0 || dev == null) return "GetDevice failed 0x" + hr.ToString("X8");
        object o;
        hr = dev.Activate(ref IID_IAudioClient, CLSCTX_ALL, IntPtr.Zero, out o);
        if (hr != 0 || o == null) return "Activate failed 0x" + hr.ToString("X8");
        IAudioClient ac = (IAudioClient)o;
        IntPtr fmt;
        hr = ac.GetMixFormat(out fmt);
        if (hr != 0) return "GetMixFormat failed 0x" + hr.ToString("X8");
        // 1s buffer (10,000,000 x 100ns), shared mode.
        hr = ac.Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, fmt, IntPtr.Zero);
        Marshal.FreeCoTaskMem(fmt);
        if (hr != 0) return "Initialize failed 0x" + hr.ToString("X8");
        uint bufFrames;
        hr = ac.GetBufferSize(out bufFrames);
        if (hr != 0) return "GetBufferSize failed 0x" + hr.ToString("X8");
        object svc;
        hr = ac.GetService(ref IID_IAudioRenderClient, out svc);
        if (hr != 0 || svc == null) return "GetService(render) failed 0x" + hr.ToString("X8");
        IAudioRenderClient rc = (IAudioRenderClient)svc;
        IntPtr buf;
        // Pre-roll a full buffer of silence, then Start so audiodg builds the chain.
        if (rc.GetBuffer((int)bufFrames, out buf) == 0) { rc.ReleaseBuffer((int)bufFrames, AUDCLNT_BUFFERFLAGS_SILENT); }
        hr = ac.Start();
        if (hr != 0) return "Start failed 0x" + hr.ToString("X8");
        DateTime end = DateTime.UtcNow.AddSeconds(seconds);
        int errors = 0;
        while (DateTime.UtcNow < end) {
            try {
                uint pad;
                int phr = ac.GetCurrentPadding(out pad);
                if (phr == 0) {
                    int avail = (int)bufFrames - (int)pad;
                    if (avail > 0 && rc.GetBuffer(avail, out buf) == 0) {
                        rc.ReleaseBuffer(avail, AUDCLNT_BUFFERFLAGS_SILENT);
                    }
                } else {
                    errors++;   // device likely invalidated (e.g. service cycle); keep the process alive
                }
            } catch {
                errors++;
            }
            Thread.Sleep(10);
        }
        try { ac.Stop(); } catch {}
        return "done (errors=" + errors + ")";
    }
}
"@

Write-Host "Holding WASAPI silence on $ScreamId for $Seconds s"
$r = [WasapiHold]::Run($ScreamId, $Seconds)
Write-Host "WasapiHold result: $r"
