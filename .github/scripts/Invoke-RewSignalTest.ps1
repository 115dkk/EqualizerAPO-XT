<#
.SYNOPSIS
    Drives REW's real Java audio generator and captures the selected render
    endpoint through WASAPI loopback.

.DESCRIPTION
    The script is intentionally application-level: REW is started with its
    official -api -nogui interface, the Java output is pointed at Scream, and a
    sweep is played while Windows captures the post-APO render stream. It does
    not redistribute REW and does not use the Pro-only measurement API.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('baseline', 'xt')]
    [string]$Phase,

    [Parameter(Mandatory = $true)]
    [string]$RewExe,

    [Parameter(Mandatory = $true)]
    [string]$EndpointId,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [double]$MinimumPeak = 0.001
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$apiLog = Join-Path $OutputDirectory "rew-$Phase-api.jsonl"
$wavPath = Join-Path $OutputDirectory "rew-$Phase-loopback.wav"
$resultPath = Join-Path $OutputDirectory "rew-$Phase-result.json"
$baseUri = 'http://127.0.0.1:4735'

function Write-ApiRecord {
    param([string]$Method, [string]$Path, $Body, $Response)

    [ordered]@{
        timestamp = (Get-Date).ToUniversalTime().ToString('o')
        method = $Method
        path = $Path
        body = $Body
        response = $Response
    } | ConvertTo-Json -Depth 12 -Compress |
        Add-Content -Path $apiLog -Encoding utf8
}

function Invoke-RewApi {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [ValidateSet('Get', 'Post', 'Put')][string]$Method = 'Get',
        $Body
    )

    $parameters = @{
        Uri = "$baseUri$Path"
        Method = $Method
        TimeoutSec = 20
    }
    if ($PSBoundParameters.ContainsKey('Body')) {
        $parameters.ContentType = 'application/json'
        $parameters.Body = $Body | ConvertTo-Json -Depth 12 -Compress
    }
    try {
        $response = Invoke-RestMethod @parameters
        Write-ApiRecord -Method $Method -Path $Path -Body $Body -Response $response
        return $response
    }
    catch {
        Write-ApiRecord -Method $Method -Path $Path -Body $Body -Response @{
            error = $_.Exception.Message
            details = $_.ErrorDetails.Message
        }
        throw
    }
}

function Get-ChoiceText {
    param($Choice)

    if ($Choice -is [string]) { return $Choice }
    foreach ($property in @('name', 'label', 'value', 'id', 'command')) {
        if ($null -ne $Choice.PSObject.Properties[$property]) {
            return [string]$Choice.$property
        }
    }
    return [string]$Choice
}

function Select-Choice {
    param($Choices, [string]$Pattern, [string]$What, [switch]$PreferShared)

    $items = @($Choices)
    $matches = @($items | Where-Object { (Get-ChoiceText $_) -match $Pattern })
    if ($PreferShared) {
        $shared = @($matches | Where-Object { (Get-ChoiceText $_) -notmatch '^(EXCL|Exclusive)' })
        if ($shared.Count -gt 0) { $matches = $shared }
    }
    if ($matches.Count -eq 0) {
        throw "REW did not report a $What matching '$Pattern': $($items | ConvertTo-Json -Depth 6 -Compress)"
    }
    return (Get-ChoiceText $matches[0])
}

function Send-RewCommand {
    param([string]$Path, [string]$Command)

    try {
        return Invoke-RewApi -Path $Path -Method Post -Body $Command
    }
    catch {
        # API betas have used both a JSON string and a ProcessCommand object.
        Write-Warning "String command body was rejected for $Path; retrying as a command object: $($_.Exception.Message)"
        return Invoke-RewApi -Path $Path -Method Post -Body @{ command = $Command }
    }
}

if (-not ('RewWasapiLoopback' -as [type])) {
    Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

public sealed class RewCaptureResult
{
    public int FormatTag { get; set; }
    public int Channels { get; set; }
    public int SampleRate { get; set; }
    public int BitsPerSample { get; set; }
    public int ValidBitsPerSample { get; set; }
    public long Frames { get; set; }
    public long Samples { get; set; }
    public int Packets { get; set; }
    public int SilentPackets { get; set; }
    public double Peak { get; set; }
    public double Rms { get; set; }
}

public static class RewWasapiLoopback
{
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    private class MMDeviceEnumerator { }

    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice endpoint);
        [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice device);
    }

    [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, int classContext, IntPtr activationParams,
            [MarshalAs(UnmanagedType.IUnknown)] out object instance);
        [PreserveSig] int OpenPropertyStore(int access, out IntPtr properties);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        [PreserveSig] int GetState(out int state);
    }

    [Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioClient
    {
        [PreserveSig] int Initialize(int shareMode, int streamFlags, long bufferDuration,
            long periodicity, IntPtr format, IntPtr sessionGuid);
        [PreserveSig] int GetBufferSize(out uint frames);
        [PreserveSig] int GetStreamLatency(out long latency);
        [PreserveSig] int GetCurrentPadding(out uint padding);
        [PreserveSig] int IsFormatSupported(int shareMode, IntPtr format, out IntPtr closestMatch);
        [PreserveSig] int GetMixFormat(out IntPtr format);
        [PreserveSig] int GetDevicePeriod(out long defaultPeriod, out long minimumPeriod);
        [PreserveSig] int Start();
        [PreserveSig] int Stop();
        [PreserveSig] int Reset();
        [PreserveSig] int SetEventHandle(IntPtr eventHandle);
        [PreserveSig] int GetService(ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object service);
    }

    [Guid("C8ADBD64-E71E-48A0-A4DE-185C395CD317"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioCaptureClient
    {
        [PreserveSig] int GetBuffer(out IntPtr data, out uint frames, out uint flags,
            out ulong devicePosition, out ulong qpcPosition);
        [PreserveSig] int ReleaseBuffer(uint frames);
        [PreserveSig] int GetNextPacketSize(out uint frames);
    }

    [DllImport("ole32.dll")]
    private static extern int CoInitializeEx(IntPtr reserved, uint coInit);

    [DllImport("ole32.dll")]
    private static extern void CoUninitialize();

    private const int CLSCTX_ALL = 0x17;
    private const int AUDCLNT_SHAREMODE_SHARED = 0;
    private const int AUDCLNT_STREAMFLAGS_LOOPBACK = 0x00020000;
    private const uint AUDCLNT_BUFFERFLAGS_SILENT = 0x00000002;
    private const ushort WAVE_FORMAT_PCM = 0x0001;
    private const ushort WAVE_FORMAT_IEEE_FLOAT = 0x0003;
    private const ushort WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

    private static void Check(int hr, string operation)
    {
        if (hr < 0) throw new COMException(operation + " failed", hr);
    }

    public static Task<RewCaptureResult> CaptureAsync(string endpointId, int durationMs, string wavePath)
    {
        return Task.Run(() => Capture(endpointId, durationMs, wavePath));
    }

    private static RewCaptureResult Capture(string endpointId, int durationMs, string wavePath)
    {
        int coHr = CoInitializeEx(IntPtr.Zero, 0);
        bool uninitialize = coHr >= 0;
        object enumeratorObject = null;
        IMMDevice device = null;
        IAudioClient audioClient = null;
        IAudioCaptureClient captureClient = null;
        IntPtr format = IntPtr.Zero;
        bool started = false;

        try
        {
            enumeratorObject = new MMDeviceEnumerator();
            var enumerator = (IMMDeviceEnumerator)enumeratorObject;
            Check(enumerator.GetDevice(endpointId, out device), "IMMDeviceEnumerator.GetDevice");

            Guid audioClientIid = new Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2");
            object audioObject;
            Check(device.Activate(ref audioClientIid, CLSCTX_ALL, IntPtr.Zero, out audioObject),
                "IMMDevice.Activate(IAudioClient)");
            audioClient = (IAudioClient)audioObject;
            Check(audioClient.GetMixFormat(out format), "IAudioClient.GetMixFormat");

            ushort formatTag = (ushort)Marshal.ReadInt16(format, 0);
            ushort channels = (ushort)Marshal.ReadInt16(format, 2);
            int sampleRate = Marshal.ReadInt32(format, 4);
            ushort blockAlign = (ushort)Marshal.ReadInt16(format, 12);
            ushort bitsPerSample = (ushort)Marshal.ReadInt16(format, 14);
            ushort extraSize = (ushort)Marshal.ReadInt16(format, 16);
            int formatSize = formatTag == WAVE_FORMAT_PCM && extraSize == 0
                ? 16
                : 18 + extraSize;
            int validBits = bitsPerSample;
            ushort sampleKind = formatTag;
            if (formatTag == WAVE_FORMAT_EXTENSIBLE && extraSize >= 22)
            {
                validBits = (ushort)Marshal.ReadInt16(format, 18);
                byte[] guidBytes = new byte[16];
                Marshal.Copy(IntPtr.Add(format, 24), guidBytes, 0, guidBytes.Length);
                sampleKind = BitConverter.ToUInt16(guidBytes, 0);
            }

            byte[] formatBytes = new byte[formatSize];
            Marshal.Copy(format, formatBytes, 0, formatBytes.Length);
            Check(audioClient.Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                10000000, 0, format, IntPtr.Zero), "IAudioClient.Initialize(loopback)");
            Guid captureIid = new Guid("C8ADBD64-E71E-48A0-A4DE-185C395CD317");
            object captureObject;
            Check(audioClient.GetService(ref captureIid, out captureObject),
                "IAudioClient.GetService(IAudioCaptureClient)");
            captureClient = (IAudioCaptureClient)captureObject;

            using (var audio = new MemoryStream())
            {
                Check(audioClient.Start(), "IAudioClient.Start");
                started = true;
                var timer = Stopwatch.StartNew();
                long framesCaptured = 0;
                int packets = 0;
                int silentPackets = 0;
                double peak = 0.0;
                double sumSquares = 0.0;
                long sampleCount = 0;

                while (timer.ElapsedMilliseconds < durationMs)
                {
                    uint packetFrames;
                    Check(captureClient.GetNextPacketSize(out packetFrames),
                        "IAudioCaptureClient.GetNextPacketSize");
                    if (packetFrames == 0)
                    {
                        Thread.Sleep(5);
                        continue;
                    }

                    IntPtr data;
                    uint frames;
                    uint flags;
                    ulong devicePosition;
                    ulong qpcPosition;
                    Check(captureClient.GetBuffer(out data, out frames, out flags,
                        out devicePosition, out qpcPosition), "IAudioCaptureClient.GetBuffer");
                    try
                    {
                        int byteCount = checked((int)(frames * blockAlign));
                        byte[] bytes = new byte[byteCount];
                        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == IntPtr.Zero)
                        {
                            silentPackets++;
                        }
                        else
                        {
                            Marshal.Copy(data, bytes, 0, byteCount);
                            Analyze(bytes, sampleKind, bitsPerSample, ref peak, ref sumSquares, ref sampleCount);
                        }
                        audio.Write(bytes, 0, bytes.Length);
                        framesCaptured += frames;
                        packets++;
                    }
                    finally
                    {
                        Check(captureClient.ReleaseBuffer(frames), "IAudioCaptureClient.ReleaseBuffer");
                    }
                }

                Check(audioClient.Stop(), "IAudioClient.Stop");
                started = false;
                WriteWave(wavePath, formatBytes, audio.ToArray());
                return new RewCaptureResult
                {
                    FormatTag = formatTag,
                    Channels = channels,
                    SampleRate = sampleRate,
                    BitsPerSample = bitsPerSample,
                    ValidBitsPerSample = validBits,
                    Frames = framesCaptured,
                    Samples = sampleCount,
                    Packets = packets,
                    SilentPackets = silentPackets,
                    Peak = peak,
                    Rms = sampleCount == 0 ? 0.0 : Math.Sqrt(sumSquares / sampleCount)
                };
            }
        }
        finally
        {
            if (started && audioClient != null) audioClient.Stop();
            if (format != IntPtr.Zero) Marshal.FreeCoTaskMem(format);
            ReleaseCom(captureClient);
            ReleaseCom(audioClient);
            ReleaseCom(device);
            ReleaseCom(enumeratorObject);
            if (uninitialize) CoUninitialize();
        }
    }

    private static void Analyze(byte[] bytes, ushort sampleKind, int bitsPerSample,
        ref double peak, ref double sumSquares, ref long sampleCount)
    {
        int bytesPerSample = bitsPerSample / 8;
        if (bytesPerSample <= 0) return;
        for (int offset = 0; offset + bytesPerSample <= bytes.Length; offset += bytesPerSample)
        {
            double value;
            if (sampleKind == WAVE_FORMAT_IEEE_FLOAT && bitsPerSample == 32)
                value = BitConverter.ToSingle(bytes, offset);
            else if (sampleKind == WAVE_FORMAT_IEEE_FLOAT && bitsPerSample == 64)
                value = BitConverter.ToDouble(bytes, offset);
            else if (sampleKind == WAVE_FORMAT_PCM && bitsPerSample == 16)
                value = BitConverter.ToInt16(bytes, offset) / 32768.0;
            else if (sampleKind == WAVE_FORMAT_PCM && bitsPerSample == 24)
            {
                int raw = bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16);
                if ((raw & 0x800000) != 0) raw |= unchecked((int)0xFF000000);
                value = raw / 8388608.0;
            }
            else if (sampleKind == WAVE_FORMAT_PCM && bitsPerSample == 32)
                value = BitConverter.ToInt32(bytes, offset) / 2147483648.0;
            else
                continue;

            if (Double.IsNaN(value) || Double.IsInfinity(value)) continue;
            double magnitude = Math.Abs(value);
            if (magnitude > peak) peak = magnitude;
            sumSquares += value * value;
            sampleCount++;
        }
    }

    private static void WriteWave(string path, byte[] format, byte[] audio)
    {
        using (var stream = File.Create(path))
        using (var writer = new BinaryWriter(stream))
        {
            int formatPadding = format.Length & 1;
            writer.Write(Encoding.ASCII.GetBytes("RIFF"));
            writer.Write(4 + 8 + format.Length + formatPadding + 8 + audio.Length);
            writer.Write(Encoding.ASCII.GetBytes("WAVE"));
            writer.Write(Encoding.ASCII.GetBytes("fmt "));
            writer.Write(format.Length);
            writer.Write(format);
            if ((format.Length & 1) != 0) writer.Write((byte)0);
            writer.Write(Encoding.ASCII.GetBytes("data"));
            writer.Write(audio.Length);
            writer.Write(audio);
        }
    }

    private static void ReleaseCom(object value)
    {
        if (value != null && Marshal.IsComObject(value)) Marshal.FinalReleaseComObject(value);
    }
}
'@
}

if (-not (Test-Path -LiteralPath $RewExe)) {
    throw "REW executable not found: $RewExe"
}

$rewProcess = $null
try {
    Get-Process -Name 'roomeqwizard' -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    $rewProcess = Start-Process -FilePath $RewExe -ArgumentList @('-api', '-nogui', '-port', '4735') -PassThru

    $deadline = (Get-Date).AddSeconds(90)
    do {
        Start-Sleep -Milliseconds 750
        try { $generatorStatus = Invoke-RewApi -Path '/generator/status' } catch { $generatorStatus = $null }
    } while ($null -eq $generatorStatus -and (Get-Date) -lt $deadline)
    if ($null -eq $generatorStatus) { throw 'REW API did not become ready within 90 seconds' }

    # Keep the exact beta API schema with the capture artifacts. Beta 130 does
    # not route GET /audio even though the published prose documents it, so
    # readiness is established through /generator/status instead.
    try {
        Invoke-WebRequest -Uri "$baseUri/doc.json" `
            -OutFile (Join-Path $OutputDirectory "rew-$Phase-openapi.json")
    }
    catch {
        Write-Warning "REW did not expose its optional OpenAPI document: $($_.Exception.Message)"
    }

    Invoke-RewApi -Path '/application/logging' -Method Post -Body $true | Out-Null
    $driver = Select-Choice (Invoke-RewApi -Path '/audio/driver-types') '^Java$' 'audio driver'
    $currentDriver = Get-ChoiceText (Invoke-RewApi -Path '/audio/driver')
    if ($currentDriver -ne $driver) {
        Invoke-RewApi -Path '/audio/driver' -Method Post -Body $driver | Out-Null
    }
    else {
        Write-Host "REW already uses the $driver audio driver"
    }

    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 750
        try { $outputDevices = Invoke-RewApi -Path '/audio/java/output-devices' } catch { $outputDevices = @() }
        $screamOutputs = @($outputDevices | Where-Object { (Get-ChoiceText $_) -match 'Scream' })
    } while ($screamOutputs.Count -eq 0 -and (Get-Date) -lt $deadline)

    $outputDevice = Select-Choice $outputDevices 'Scream' 'Java output device' -PreferShared
    Invoke-RewApi -Path '/audio/java/output-device' -Method Post -Body $outputDevice | Out-Null
    Start-Sleep -Seconds 2

    $outputs = Invoke-RewApi -Path '/audio/java/outputs'
    if (@($outputs).Count -gt 0) {
        $output = Get-ChoiceText (@($outputs)[0])
        Invoke-RewApi -Path '/audio/java/output' -Method Post -Body $output | Out-Null
    }
    $outputChannels = Invoke-RewApi -Path '/audio/java/output-channels'
    $stereo = @($outputChannels | Where-Object { (Get-ChoiceText $_) -match 'L\+R|Stereo' } | Select-Object -First 1)
    if ($stereo.Count -gt 0) {
        Invoke-RewApi -Path '/audio/java/output-channel' -Method Post -Body (Get-ChoiceText $stereo[0]) | Out-Null
    }

    $signal = Select-Choice (Invoke-RewApi -Path '/generator/signals') 'sweep' 'generator sweep signal'
    Invoke-RewApi -Path '/generator/signal' -Method Put -Body $signal | Out-Null
    $level = Invoke-RewApi -Path '/generator/level'
    if ($level -is [double] -or $level -is [int] -or $level -is [string]) {
        Invoke-RewApi -Path '/generator/level' -Method Post -Body '-18 dBFS' | Out-Null
    }
    else {
        $levelBody = @{ value = -18.0; unit = 'dBFS' }
        Invoke-RewApi -Path '/generator/level' -Method Post -Body $levelBody | Out-Null
    }

    $commands = Invoke-RewApi -Path '/generator/commands'
    $play = Select-Choice $commands '^(Start|Play)' 'generator start command'
    $stop = Select-Choice $commands '^(Stop)' 'generator stop command'

    $capture = [RewWasapiLoopback]::CaptureAsync($EndpointId, 8000, $wavPath)
    Start-Sleep -Milliseconds 750
    Send-RewCommand -Path '/generator/commands' -Command $play | Out-Null
    Start-Sleep -Seconds 5
    Send-RewCommand -Path '/generator/commands' -Command $stop | Out-Null
    $result = $capture.GetAwaiter().GetResult()

    $verdict = [ordered]@{
        phase = $Phase
        endpointId = $EndpointId
        javaOutputDevice = $outputDevice
        signal = $signal
        minimumPeak = $MinimumPeak
        signalDetected = ($result.Peak -ge $MinimumPeak)
        capture = $result
    }
    $verdict | ConvertTo-Json -Depth 8 |
        Out-File -FilePath $resultPath -Encoding utf8
    $verdict | ConvertTo-Json -Depth 8 | Write-Host

    if (-not $verdict.signalDetected) {
        if ($Phase -eq 'baseline') {
            throw "HARNESS BASELINE FAILED: REW produced no detectable Scream loopback signal (peak=$($result.Peak), threshold=$MinimumPeak)"
        }
        throw "XT RED: baseline was valid but the REW signal became silent after the APO was applied (peak=$($result.Peak), threshold=$MinimumPeak)"
    }
}
finally {
    foreach ($path in @('/generator/status', '/application/errors', '/application/warnings')) {
        try { Invoke-RewApi -Path $path | Out-Null }
        catch { Write-Warning "Could not collect REW diagnostic endpoint $path`: $($_.Exception.Message)" }
    }
    try {
        $commands = Invoke-RewApi -Path '/application/commands'
        $shutdown = Select-Choice $commands '^Shutdown$' 'application shutdown command'
        Send-RewCommand -Path '/application/command' -Command $shutdown | Out-Null
    }
    catch {
        Write-Warning "Could not shut REW down through the API: $($_.Exception.Message)"
    }
    if ($rewProcess -and -not $rewProcess.HasExited) {
        if (-not $rewProcess.WaitForExit(10000)) { $rewProcess.Kill() }
    }
}
