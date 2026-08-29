<#
.SYNOPSIS
    The capture gate: the EQ APO must process a recording endpoint the way a
    recording application hears it.

.DESCRIPTION
    A hosted runner has no audio hardware, so the gate installs VB-CABLE, a
    signed virtual cable driver: whatever is played into its "CABLE Input"
    playback endpoint comes out of its "CABLE Output" recording endpoint. The
    built product is staged and registered through its own install hook
    (Editor.exe --veloapp-install), a config that says "Preamp: -20 dB" is
    written, and the cable's recording side is measured with
    Tests/CaptureProbe, which plays a sine into the cable and records from it
    through WASAPI shared mode - the path every recording app uses, and the
    one that makes the audio engine build the endpoint's APO chain.

    Phases, each with a verdict in the summary:

      baseline        the cable at unity before any APO (the gate's own sanity)
      apo-host        Tests/ApoHostProbe drives EqualizerAPO.dll for the
                      capture endpoint without the audio engine: the DLL's own
                      capture branch must apply the preamp
      install         DeviceSelector --install-endpoint puts the APO on the
                      recording endpoint the way the dialog's OK does and runs
                      the device test (exit 0 = the APO reported Initialize
                      from inside the audio engine)
      apo-default     the recording app hears the tone $PreampDb dB down
      apo-comms       the same for a stream tagged Communications (voice chat)
      apo-raw         a raw-mode stream, informational: raw bypasses stream
                      effects by design
      uninstall       DeviceSelector --uninstall-endpoint, then the cable is at
                      unity again and the endpoint's effect chain holds no EQ
                      CLSID

    The install/measure/uninstall round runs once per install mode: first
    the mode the product picks on its own for this endpoint (a virtual
    cable publishes no effect chain, so that is the interesting one), then
    each of the three modes by name. Only the product's own choice is
    gated. The named rounds are the evidence behind that choice, measured
    on the third run: on this cable the legacy LFX slot carries the stream
    (-20 dB) and a stream-slot (SFX) registration is never loaded by the
    audio engine at all (unity, no Initialize in its log, device test
    fails). VB-CABLE is a legacy, mode-unaware driver, and the engine feeds
    those through the legacy slots; the product's LFX/GFX default for a
    device without a driver chain is what keeps such microphones working.

    Snapshots (registry, the audio engine's APO log, DeviceSelector.log, the
    probe outputs) land in -SnapshotDirectory for the job to upload.

    -PlanOnly returns the plan without touching the machine, for Pester.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $WorkspaceRoot,
    [string] $ArtifactPath,
    [string] $ProbeDirectory,
    [string] $StageRoot = "C:\EqualizerAPO-XT-capture-gate",
    [string] $SnapshotDirectory,
    [string] $VbCableUrl = "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack43.zip",
    [string] $VbCableSha256 = "66FD0A4D9F4896FF41632B7E3D53892C085C4561F53E8AE8D0F0BC10EEDD1CDD",
    [double] $PreampDb = -20.0,
    [double] $ToleranceDb = 1.0,
    [switch] $SkipDriverInstall,
    [switch] $PlanOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$renderConnection = "CABLE Input"
$captureConnection = "CABLE Output"
$measurements = @(
    [pscustomobject]@{ Name = "baseline";         Category = "default";        Raw = $false; ExpectGainDb = 0.0;       ToleranceDb = 1.5;          Required = $true;  Note = "the cable at unity before any APO" }
    [pscustomobject]@{ Name = "apo-default";      Category = "default";        Raw = $false; ExpectGainDb = $PreampDb; ToleranceDb = $ToleranceDb; Required = $true;  Note = "a recording app hears the preamp" }
    [pscustomobject]@{ Name = "apo-comms";        Category = "communications"; Raw = $false; ExpectGainDb = $PreampDb; ToleranceDb = $ToleranceDb; Required = $true;  Note = "a voice-chat stream hears the preamp" }
    [pscustomobject]@{ Name = "apo-raw";          Category = "default";        Raw = $true;  ExpectGainDb = 0.0;       ToleranceDb = 1.5;          Required = $false; Note = "raw mode bypasses stream effects by design" }
    [pscustomobject]@{ Name = "after-uninstall";  Category = "default";        Raw = $false; ExpectGainDb = 0.0;       ToleranceDb = 1.5;          Required = $true;  Note = "the cable at unity after the uninstall" }
)

$installModes = @(
    [pscustomobject]@{ Name = "default"; Arguments = @();                              Required = $true }
    [pscustomobject]@{ Name = "sfx-efx"; Arguments = @("--install-mode", "sfx-efx"); Required = $false }
    [pscustomobject]@{ Name = "sfx-mfx"; Arguments = @("--install-mode", "sfx-mfx"); Required = $false }
    [pscustomobject]@{ Name = "lfx-gfx"; Arguments = @("--install-mode", "lfx-gfx"); Required = $false }
)
# The low-latency round (docs/architecture/wasapi-exclusive-study.md, section
# 3): the APO on the cable's playback endpoint, a convolution in the config (a
# unit impulse, so unity, but the one filter that notices the block length),
# and the tone stream opened through IAudioClient3 at the engine's smallest
# period, once fresh and once after a default-period stream already runs on
# the endpoint, so the engine has to switch a running graph. A convolution
# that goes silent there means the engine hands the post-mix APO blocks
# shorter than the count it locked with. Skipped, and said so, when the
# runner's cable declares no period below the default.
$lowLatencyMeasurements = @(
    [pscustomobject]@{ Name = "ll-default";         Category = "default"; Raw = $false; Period = "default"; HoldDefault = $false; ExpectGainDb = $PreampDb; ToleranceDb = $ToleranceDb; Required = $true; Note = "the convolution config at the default period" }
    [pscustomobject]@{ Name = "ll-min";             Category = "default"; Raw = $false; Period = "min";     HoldDefault = $false; ExpectGainDb = $PreampDb; ToleranceDb = 3.0;          Required = $true; Note = "a fresh small-period stream reaches the convolution" }
    [pscustomobject]@{ Name = "ll-min-switch";      Category = "default"; Raw = $false; Period = "min";     HoldDefault = $true;  ExpectGainDb = $PreampDb; ToleranceDb = 3.0;          Required = $true; Note = "the engine switches a running default-period graph to the small period" }
    [pscustomobject]@{ Name = "ll-after-uninstall"; Category = "default"; Raw = $false; Period = "";        HoldDefault = $false; ExpectGainDb = 0.0;       ToleranceDb = 1.5;          Required = $true; Note = "the cable at unity after the playback-side uninstall" }
)

$plan = [pscustomobject]@{
    VbCableUrl = $VbCableUrl
    VbCableSha256 = $VbCableSha256
    RenderConnection = $renderConnection
    CaptureConnection = $captureConnection
    PreampDb = $PreampDb
    Measurements = $measurements
    InstallModes = $installModes
    LowLatency = $lowLatencyMeasurements
    StageRoot = $StageRoot
}
if ($PlanOnly) { return $plan }

foreach ($required in @("ArtifactPath", "ProbeDirectory", "SnapshotDirectory")) {
    if ([string]::IsNullOrWhiteSpace((Get-Variable -Name $required -ValueOnly))) {
        throw "-$required is required unless -PlanOnly"
    }
}
New-Item -ItemType Directory -Force -Path $SnapshotDirectory | Out-Null
$captureProbe = Join-Path $ProbeDirectory "CaptureProbe.exe"
$apoHostProbe = Join-Path $ProbeDirectory "ApoHostProbe.exe"
foreach ($exe in @($captureProbe, $apoHostProbe)) {
    if (-not (Test-Path -LiteralPath $exe)) { throw "probe not found: $exe" }
}

$summary = [ordered]@{
    driver = $null
    endpoints = $null
    stage = $null
    apoHost = $null
    rounds = @()
    lowLatency = $null
    measurements = @()
    failures = @()
}

$roundRequired = $true

function Write-Phase([string] $title) {
    Write-Host ""
    Write-Host "== $title"
}

function Add-Failure([string] $what) {
    $summary.failures += $what
    Write-Host "::error::$what"
}

# Runs a program, waits (with a deadline), and returns exit code and output.
# GUI-subsystem programs (DeviceSelector) print to the console they attach
# to and to their log; their stdout here is often empty, and their exit code
# is the verdict.
function Invoke-Program([string] $file, [string[]] $arguments, [int] $timeoutSeconds, [string] $workingDirectory) {
    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()
    $quoted = @($arguments | ForEach-Object {
        if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
    })
    $startArgs = @{
        FilePath = $file
        ArgumentList = $quoted
        PassThru = $true
        NoNewWindow = $true
        RedirectStandardOutput = $stdout
        RedirectStandardError = $stderr
    }
    if ($workingDirectory) { $startArgs.WorkingDirectory = $workingDirectory }
    $process = Start-Process @startArgs
    $exited = $process.WaitForExit($timeoutSeconds * 1000)
    if (-not $exited) {
        Write-Warning "$file did not exit within ${timeoutSeconds}s; killing it"
        try { $process.Kill() } catch {}
        $process.WaitForExit(5000) | Out-Null
    }
    $result = [pscustomobject]@{
        ExitCode = if ($exited) { $process.ExitCode } else { -1 }
        StdOut = (Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue)
        StdErr = (Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue)
        TimedOut = -not $exited
    }
    Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
    if ($result.StdErr) { Write-Host ($result.StdErr.TrimEnd()) }
    if ($result.StdOut) { Write-Host ($result.StdOut.TrimEnd()) }
    return $result
}

function Get-EndpointGuid([string] $flow, [string] $connectionName) {
    $root = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\$flow"
    if (-not (Test-Path $root)) { return $null }
    foreach ($key in Get-ChildItem $root -ErrorAction SilentlyContinue) {
        $properties = Get-ItemProperty -Path "$($key.PSPath)\Properties" -ErrorAction SilentlyContinue
        if ($null -eq $properties) { continue }
        $connection = $properties."{a45c254e-df1c-4efd-8020-67d146a850e0},2"
        $state = (Get-ItemProperty -Path $key.PSPath -Name DeviceState -ErrorAction SilentlyContinue).DeviceState
        if ($connection -eq $connectionName -and [int]$state -eq 1) { return $key.PSChildName }
    }
    return $null
}

function Wait-CableEndpoints([int] $timeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    do {
        $render = Get-EndpointGuid "Render" $renderConnection
        $capture = Get-EndpointGuid "Capture" $captureConnection
        if ($render -and $capture) {
            return [pscustomobject]@{ Render = $render; Capture = $capture }
        }
        Start-Sleep -Seconds 3
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Save-EndpointSnapshot([string] $endpointGuid, [string] $phase, [string] $flow = "Capture") {
    $keyPath = "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\$flow\$endpointGuid"
    & reg export $keyPath (Join-Path $SnapshotDirectory "$phase-$($flow.ToLower())-endpoint.reg") /y | Out-Null
    & reg export "HKLM\SOFTWARE\EqualizerAPO" (Join-Path $SnapshotDirectory "$phase-equalizerapo.reg") /y 2>$null | Out-Null
    $global:LASTEXITCODE = 0
    $fx = Get-ItemProperty -Path "Registry::$keyPath\FxProperties" -ErrorAction SilentlyContinue
    if ($fx) {
        $fx | Select-Object -Property * -ExcludeProperty PS* | ConvertTo-Json -Depth 3 |
            Out-File -FilePath (Join-Path $SnapshotDirectory "$phase-fxproperties.json") -Encoding utf8
    }
}

# 16-bit mono PCM, the first sample at full scale and the rest zero: the
# identity for a convolution (-0.0003 dB), in a container libsndfile reads
# without a codec.
function Write-ImpulseWav([string] $path, [int] $rate, [int] $frames) {
    $dataBytes = $frames * 2
    $stream = [System.IO.File]::Create($path)
    try {
        $writer = New-Object System.IO.BinaryWriter($stream)
        $writer.Write([byte[]][char[]]"RIFF"); $writer.Write([int32](36 + $dataBytes)); $writer.Write([byte[]][char[]]"WAVE")
        $writer.Write([byte[]][char[]]"fmt "); $writer.Write([int32]16); $writer.Write([int16]1); $writer.Write([int16]1)
        $writer.Write([int32]$rate); $writer.Write([int32]($rate * 2)); $writer.Write([int16]2); $writer.Write([int16]16)
        $writer.Write([byte[]][char[]]"data"); $writer.Write([int32]$dataBytes)
        $writer.Write([int16]32767)
        for ($i = 1; $i -lt $frames; $i++) { $writer.Write([int16]0) }
        $writer.Flush()
    } finally {
        $stream.Dispose()
    }
}

# The effect chain an endpoint's FxProperties names, as "LFX=... GFX=...".
function Get-EffectChain([string] $flow, [string] $endpointGuid) {
    $fx = Get-ItemProperty -Path "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\$flow\$endpointGuid\FxProperties" -ErrorAction SilentlyContinue
    if (-not $fx) { return $null }
    $slots = @()
    foreach ($slot in @(@("LFX", "1"), @("GFX", "2"), @("SFX", "5"), @("MFX", "6"), @("EFX", "7"))) {
        $property = $fx.PSObject.Properties["{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},$($slot[1])"]
        if ($property -and $property.Value) { $slots += "$($slot[0])=$($property.Value)" }
    }
    return ($slots -join " ")
}

function Test-EqClsidLeft([string] $flow, [string] $endpointGuid) {
    $fx = Get-ItemProperty -Path "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\$flow\$endpointGuid\FxProperties" -ErrorAction SilentlyContinue
    if (-not $fx) { return $false }
    foreach ($property in $fx.PSObject.Properties) {
        if ("$($property.Value)" -match "EACD2258-FCAC-4FF4-B36D-419E924A6D79|EC1CC9CE-FAED-4822-828A-82A81A6F018F") { return $true }
    }
    return $false
}

function Get-JsonField($json, [string] $name) {
    if ($null -eq $json) { return $null }
    $property = $json.PSObject.Properties[$name]
    if ($property) { return $property.Value } else { return $null }
}

function Copy-Logs([string] $phase) {
    $apoLog = "C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp\EqualizerAPO.log"
    if (Test-Path -LiteralPath $apoLog) {
        Copy-Item -LiteralPath $apoLog -Destination (Join-Path $SnapshotDirectory "$phase-audiodg-EqualizerAPO.log") -Force
    }
    $userLogs = Join-Path $env:LOCALAPPDATA "EqualizerAPO\logs"
    if (Test-Path -LiteralPath $userLogs) {
        foreach ($log in Get-ChildItem -LiteralPath $userLogs -File) {
            Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $SnapshotDirectory "$phase-$($log.Name)") -Force
        }
    }
}

function Measure-Cable($measurement, [string] $round = "") {
    $arguments = @("--render", $renderConnection, "--capture", $captureConnection, "--json",
        "--expect-gain-db", $measurement.ExpectGainDb.ToString([cultureinfo]::InvariantCulture),
        "--tolerance-db", $measurement.ToleranceDb.ToString([cultureinfo]::InvariantCulture))
    if ($measurement.Category -ne "default") { $arguments += @("--category", $measurement.Category) }
    if ($measurement.Raw) { $arguments += "--raw" }
    $period = if ($measurement.PSObject.Properties["Period"]) { [string]$measurement.Period } else { "" }
    if ($period) { $arguments += @("--period", $period) }
    if ($measurement.PSObject.Properties["HoldDefault"] -and $measurement.HoldDefault) { $arguments += "--hold-default" }
    $run = Invoke-Program $captureProbe $arguments 60
    $json = $null
    if ($run.StdOut) {
        $line = ($run.StdOut -split "`n" | Where-Object { $_.Trim().StartsWith("{") } | Select-Object -Last 1)
        if ($line) { $json = $line | ConvertFrom-Json }
    }
    $label = if ($round) { "$round/$($measurement.Name)" } else { $measurement.Name }
    $record = [ordered]@{
        name = $label
        round = $round
        category = $measurement.Category
        raw = $measurement.Raw
        expectGainDb = $measurement.ExpectGainDb
        toleranceDb = $measurement.ToleranceDb
        required = $measurement.Required
        exitCode = $run.ExitCode
        gainDb = if ($json) { $json.gainDb } else { $null }
        toneDb = if ($json) { $json.toneDb } else { $null }
        rmsDb = if ($json) { $json.rmsDb } else { $null }
        silentPackets = if ($json) { $json.silentPackets } else { $null }
        rate = if ($json) { $json.rate } else { $null }
        period = $period
        renderPeriodFrames = Get-JsonField $json "renderPeriodFrames"
        engineDefaultPeriodFrames = Get-JsonField $json "engineDefaultPeriodFrames"
        engineMinPeriodFrames = Get-JsonField $json "engineMinPeriodFrames"
        enginePeriodFrames = Get-JsonField $json "enginePeriodFrames"
        holdEnginePeriodFrames = Get-JsonField $json "holdEnginePeriodFrames"
        smallPeriodAvailable = $null
        passed = ($run.ExitCode -eq 0)
        note = $measurement.Note
    }
    # A "min" request that came back at the default period says the driver
    # declares no smaller one: nothing to judge, and the summary says so.
    $noSmallPeriod = $false
    if ($period -eq "min" -and $null -ne $record.engineMinPeriodFrames -and $null -ne $record.engineDefaultPeriodFrames) {
        $record.smallPeriodAvailable = ([int]$record.engineMinPeriodFrames -lt [int]$record.engineDefaultPeriodFrames)
        $noSmallPeriod = -not $record.smallPeriodAvailable
        if ($noSmallPeriod) { $record.note = "$($measurement.Note); the driver declares no period below the default ($($record.engineDefaultPeriodFrames) frames), nothing to judge" }
    }
    $summary.measurements += [pscustomobject]$record
    $verdict = if ($noSmallPeriod) { "skipped (no small period)" } elseif ($record.passed) { "ok" } elseif ($measurement.Required) { "FAILED" } else { "noted" }
    Write-Host ("measurement {0}: gain {1} dB (expected {2} +- {3}) -> {4}" -f $label, $record.gainDb, $measurement.ExpectGainDb, $measurement.ToleranceDb, $verdict)
    if ($period) {
        Write-Host ("  playback period {0} frames (engine default {1}, min {2}); engine ran at {3}, held stream at {4}" -f $record.renderPeriodFrames, $record.engineDefaultPeriodFrames, $record.engineMinPeriodFrames, $record.enginePeriodFrames, $record.holdEnginePeriodFrames)
    }
    if (-not $record.passed -and $measurement.Required -and $roundRequired -and -not $noSmallPeriod) {
        Add-Failure ("{0}: the tone arrived at {1} dB, expected {2} +- {3} dB ({4})" -f $label, $record.gainDb, $measurement.ExpectGainDb, $measurement.ToleranceDb, $measurement.Note)
    }
    return $record
}

# ---------------------------------------------------------------------------
Write-Phase "audio services"
foreach ($service in @("AudioEndpointBuilder", "AudioSrv")) {
    try { Set-Service -Name $service -StartupType Automatic -ErrorAction Stop } catch { Write-Warning "Set-Service ${service}: $($_.Exception.Message)" }
    try { Start-Service -Name $service -ErrorAction Stop } catch { Write-Warning "Start-Service ${service}: $($_.Exception.Message)" }
}
Get-Service AudioEndpointBuilder, AudioSrv | Format-Table -AutoSize Name, Status | Out-String | Write-Host

# ---------------------------------------------------------------------------
Write-Phase "microphone access"
# Second run on the runner: every capture stream, the probe's and the device
# test's alike, failed with E_ACCESSDENIED. Windows gates microphone access
# per app through the privacy settings, and a server image denies it by
# default. The consent store is what the Settings toggle writes; the machine
# and the user hive both, and the NonPackaged branch that desktop programs
# fall under.
$consentRoots = @(
    "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone",
    "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone"
)
foreach ($root in $consentRoots) {
    foreach ($key in @($root, "$root\NonPackaged")) {
        New-Item -Path $key -Force | Out-Null
        Set-ItemProperty -Path $key -Name Value -Value "Allow" -Type String
    }
}
New-Item -Path "HKLM:\SOFTWARE\Policies\Microsoft\Windows\AppPrivacy" -Force | Out-Null
Set-ItemProperty -Path "HKLM:\SOFTWARE\Policies\Microsoft\Windows\AppPrivacy" -Name LetAppsAccessMicrophone -Value 1 -Type DWord
Write-Host "microphone consent: Allow (machine and user, packaged and desktop)"

# ---------------------------------------------------------------------------
Write-Phase "virtual cable"
$endpoints = Wait-CableEndpoints 1
if ($endpoints) {
    Write-Host "VB-CABLE endpoints already present"
    $summary.driver = "present"
} elseif ($SkipDriverInstall) {
    throw "no VB-CABLE endpoints and -SkipDriverInstall was given"
} else {
    $work = Join-Path ([System.IO.Path]::GetTempPath()) "vbcable-gate"
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    $zip = Join-Path $work "vbcable.zip"
    $attempt = 0
    do {
        $attempt++
        try {
            Invoke-WebRequest -Uri $VbCableUrl -OutFile $zip -UseBasicParsing -TimeoutSec 120
            break
        } catch {
            if ($attempt -ge 3) { throw }
            Write-Warning "download attempt $attempt failed: $($_.Exception.Message)"
            Start-Sleep -Seconds 15
        }
    } while ($true)
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    if ($hash -ne $VbCableSha256.ToUpperInvariant()) {
        throw "VB-CABLE download hash $hash does not match the pinned $VbCableSha256"
    }
    $unpacked = Join-Path $work "unpacked"
    if (Test-Path $unpacked) { Remove-Item -LiteralPath $unpacked -Recurse -Force }
    Expand-Archive -LiteralPath $zip -DestinationPath $unpacked -Force

    # The driver is signed by its publisher. Trusting the signer in advance is
    # what keeps the "install this device software?" dialog from appearing on
    # a headless runner.
    $catalog = Get-ChildItem -LiteralPath $unpacked -Filter "vbaudio_cable64_win7.cat" -Recurse | Select-Object -First 1
    if ($catalog) {
        $signature = Get-AuthenticodeSignature -LiteralPath $catalog.FullName
        Write-Host "driver catalog signature: $($signature.Status) $($signature.SignerCertificate.Subject)"
        if ($signature.SignerCertificate) {
            $cer = Join-Path $work "vbaudio-signer.cer"
            Export-Certificate -Cert $signature.SignerCertificate -FilePath $cer -Force | Out-Null
            Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
        }
    }

    $setup = Get-ChildItem -LiteralPath $unpacked -Filter "VBCABLE_Setup_x64.exe" -Recurse | Select-Object -First 1
    if (-not $setup) { throw "VBCABLE_Setup_x64.exe not found in the pack" }
    Write-Host "running $($setup.FullName) -i -h"
    $setupRun = Invoke-Program $setup.FullName @("-i", "-h") 240 $setup.DirectoryName
    Write-Host "setup exit code $($setupRun.ExitCode)$(if ($setupRun.TimedOut) { ' (timed out)' })"

    foreach ($service in @("AudioEndpointBuilder", "AudioSrv")) {
        try { Start-Service -Name $service -ErrorAction Stop } catch { Write-Warning "Start-Service ${service}: $($_.Exception.Message)" }
    }
    try { Restart-Service -Name AudioEndpointBuilder -Force -ErrorAction Stop } catch { Write-Warning "Restart AudioEndpointBuilder: $($_.Exception.Message)" }
    try { Start-Service -Name AudioSrv -ErrorAction Stop } catch { Write-Warning "Start-Service AudioSrv: $($_.Exception.Message)" }
    $endpoints = Wait-CableEndpoints 90
    if (-not $endpoints) {
        & pnputil /enum-devices /class MEDIA 2>&1 | Out-File -FilePath (Join-Path $SnapshotDirectory "pnputil-media-devices.txt") -Encoding utf8
        $global:LASTEXITCODE = 0
        throw "VB-CABLE produced no active '$renderConnection' / '$captureConnection' endpoints (setup exit $($setupRun.ExitCode))"
    }
    $summary.driver = "installed"
}
$summary.endpoints = [ordered]@{ render = $endpoints.Render; capture = $endpoints.Capture }
Write-Host "render endpoint  $($endpoints.Render)"
Write-Host "capture endpoint $($endpoints.Capture)"
Save-EndpointSnapshot $endpoints.Capture "10-before"

# ---------------------------------------------------------------------------
Write-Phase "stage the product"
$current = Join-Path $StageRoot "current"
if (Test-Path -LiteralPath $current) { Remove-Item -LiteralPath $current -Recurse -Force }
New-Item -ItemType Directory -Force -Path $current | Out-Null
Copy-Item -Path (Join-Path $ArtifactPath "*") -Destination $current -Recurse -Force
foreach ($file in @("EqualizerAPO.dll", "Editor.exe", "DeviceSelector.exe")) {
    if (-not (Test-Path -LiteralPath (Join-Path $current $file))) { throw "artifact lacks $file" }
}
# The product's own install hook: HKLM vocabulary, DisableProtectedAudioDG,
# the ACL the audio engine needs on the tree, DllRegisterServer.
$hook = Invoke-Program (Join-Path $current "Editor.exe") @("--veloapp-install") 180 $current
if ($hook.ExitCode -ne 0) { throw "Editor.exe --veloapp-install exited with $($hook.ExitCode)" }
$app = Get-ItemProperty -Path "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO"
$configDir = $app.ConfigPath
if (-not $configDir) { throw "the install hook left no ConfigPath" }
New-Item -ItemType Directory -Force -Path $configDir | Out-Null
$configFile = Join-Path $configDir "config.txt"
$config = "Preamp: $($PreampDb.ToString([cultureinfo]::InvariantCulture)) dB`r`n"
[System.IO.File]::WriteAllText($configFile, $config, (New-Object System.Text.UTF8Encoding($false)))
# The audio engine's copy of the DLL writes its trace to its own temp; that
# file is what tells a capture failure apart from a silent success.
& reg add "HKLM\SOFTWARE\EqualizerAPO" /v EnableTrace /t REG_SZ /d true /f | Out-Null
$summary.stage = [ordered]@{ installPath = $app.InstallPath; configPath = $configDir; config = $config.Trim() }
Write-Host "InstallPath $($app.InstallPath)"
Write-Host "ConfigPath  $configDir  ($($config.Trim()))"

# ---------------------------------------------------------------------------
Write-Phase "baseline"
Measure-Cable $measurements[0] | Out-Null

# ---------------------------------------------------------------------------
Write-Phase "the DLL hosted for the capture endpoint (no audio engine)"
$hostRun = Invoke-Program $apoHostProbe @("--dll", (Join-Path $current "EqualizerAPO.dll"),
    "--endpoint", $endpoints.Capture, "--json",
    "--expect-gain-db", $PreampDb.ToString([cultureinfo]::InvariantCulture), "--tolerance-db", "0.5") 60
$summary.apoHost = [ordered]@{ exitCode = $hostRun.ExitCode; output = ($hostRun.StdOut + $hostRun.StdErr) }
if ($hostRun.ExitCode -ne 0) {
    Add-Failure "apo-host: EqualizerAPO.dll hosted for the capture endpoint did not apply the preamp (exit $($hostRun.ExitCode))"
}

# ---------------------------------------------------------------------------
foreach ($mode in $installModes) {
    $round = $mode.Name
    $roundRequired = $mode.Required
    $roundRecord = [ordered]@{ mode = $round; install = $null; installMode = $null; uninstall = $null }

    Write-Phase "install the APO on the recording endpoint ($round)"
    $install = Invoke-Program (Join-Path $current "DeviceSelector.exe") (@("--install-endpoint", $endpoints.Capture) + $mode.Arguments) 300 $current
    $roundRecord.install = [ordered]@{ exitCode = $install.ExitCode; timedOut = $install.TimedOut }
    Save-EndpointSnapshot $endpoints.Capture "30-$round-installed"
    $fxNow = Get-ItemProperty -Path "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\$($endpoints.Capture)\FxProperties" -ErrorAction SilentlyContinue
    if ($fxNow) {
        $slots = @()
        foreach ($slot in @(@("LFX", "1"), @("GFX", "2"), @("SFX", "5"), @("MFX", "6"), @("EFX", "7"))) {
            $property = $fxNow.PSObject.Properties["{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},$($slot[1])"]
            if ($property -and $property.Value) { $slots += "$($slot[0])=$($property.Value)" }
        }
        $roundRecord.installMode = ($slots -join " ")
        Write-Host "effect chain now: $($roundRecord.installMode)"
    }
    if ($install.ExitCode -ne 0 -and $roundRequired) {
        Add-Failure "$round/install: DeviceSelector --install-endpoint exited with $($install.ExitCode) (the device test did not see the APO come up)"
    }
    Copy-Logs "30-$round-installed"

    Write-Phase "measure with the APO ($round)"
    Measure-Cable $measurements[1] $round | Out-Null
    Measure-Cable $measurements[2] $round | Out-Null
    Measure-Cable $measurements[3] $round | Out-Null
    Copy-Logs "40-$round-measured"

    Write-Phase "uninstall ($round)"
    $uninstall = Invoke-Program (Join-Path $current "DeviceSelector.exe") @("--uninstall-endpoint", $endpoints.Capture) 180 $current
    $fxAfter = Get-ItemProperty -Path "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\$($endpoints.Capture)\FxProperties" -ErrorAction SilentlyContinue
    $eqLeft = $false
    if ($fxAfter) {
        foreach ($property in $fxAfter.PSObject.Properties) {
            if ("$($property.Value)" -match "EACD2258-FCAC-4FF4-B36D-419E924A6D79|EC1CC9CE-FAED-4822-828A-82A81A6F018F") { $eqLeft = $true }
        }
    }
    $roundRecord.uninstall = [ordered]@{ exitCode = $uninstall.ExitCode; eqClsidLeft = $eqLeft }
    Save-EndpointSnapshot $endpoints.Capture "50-$round-uninstalled"
    if ($uninstall.ExitCode -ne 0) { Add-Failure "$round/uninstall: DeviceSelector --uninstall-endpoint exited with $($uninstall.ExitCode)" }
    if ($eqLeft) { Add-Failure "$round/uninstall: the endpoint's FxProperties still names an EQ APO CLSID" }
    Measure-Cable $measurements[4] $round | Out-Null
    Copy-Logs "50-$round-uninstalled"
    $summary.rounds += [pscustomobject]$roundRecord
}
$roundRequired = $true

# ---------------------------------------------------------------------------
Write-Phase "low-latency: the playback endpoint, a convolution, the engine's smallest period"
$baseline = $summary.measurements | Where-Object { $_.name -eq "baseline" } | Select-Object -First 1
$impulseRate = if ($baseline -and $baseline.rate) { [int]$baseline.rate } else { 48000 }
$impulse = Join-Path $configDir "gate-impulse.wav"
Write-ImpulseWav $impulse $impulseRate 4096
$lowLatencyConfig = "Preamp: $($PreampDb.ToString([cultureinfo]::InvariantCulture)) dB`r`nConvolution: gate-impulse.wav`r`n"
[System.IO.File]::WriteAllText($configFile, $lowLatencyConfig, (New-Object System.Text.UTF8Encoding($false)))
$lowLatency = [ordered]@{
    config = $lowLatencyConfig.Trim()
    impulseRate = $impulseRate
    install = $null
    installMode = $null
    uninstall = $null
    lockFrameCounts = @()
    smallPeriodAvailable = $null
}
Write-Host "config now: $($lowLatencyConfig.Trim() -replace "`r`n", ' | ') (impulse at $impulseRate Hz)"

$install = Invoke-Program (Join-Path $current "DeviceSelector.exe") @("--install-endpoint", $endpoints.Render) 300 $current
$lowLatency.install = [ordered]@{ exitCode = $install.ExitCode; timedOut = $install.TimedOut }
Save-EndpointSnapshot $endpoints.Render "60-low-latency-installed" "Render"
$lowLatency.installMode = Get-EffectChain "Render" $endpoints.Render
Write-Host "effect chain now (playback): $($lowLatency.installMode)"
if ($install.ExitCode -ne 0) {
    Add-Failure "low-latency/install: DeviceSelector --install-endpoint on the playback endpoint exited with $($install.ExitCode)"
}
Copy-Logs "60-low-latency-installed"

Measure-Cable $lowLatencyMeasurements[0] "low-latency" | Out-Null
Measure-Cable $lowLatencyMeasurements[1] "low-latency" | Out-Null
Measure-Cable $lowLatencyMeasurements[2] "low-latency" | Out-Null
Copy-Logs "70-low-latency-measured"
# Every frame count the audio engine locked the DLL with, in order: the last
# field of the trace line LockForProcess writes for its input connection.
$apoLog = "C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp\EqualizerAPO.log"
if (Test-Path -LiteralPath $apoLog) {
    $lowLatency.lockFrameCounts = @(Select-String -LiteralPath $apoLog -Pattern "Input format in LockForProcess = \{.*, (\d+) \}" |
        ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
}
Write-Host "LockForProcess max frame counts, whole run: $($lowLatency.lockFrameCounts -join ' ')"
$minRecord = $summary.measurements | Where-Object { $_.name -eq "low-latency/ll-min" } | Select-Object -First 1
if ($minRecord) { $lowLatency.smallPeriodAvailable = $minRecord.smallPeriodAvailable }

$uninstall = Invoke-Program (Join-Path $current "DeviceSelector.exe") @("--uninstall-endpoint", $endpoints.Render) 180 $current
$eqLeftRender = Test-EqClsidLeft "Render" $endpoints.Render
$lowLatency.uninstall = [ordered]@{ exitCode = $uninstall.ExitCode; eqClsidLeft = $eqLeftRender }
Save-EndpointSnapshot $endpoints.Render "80-low-latency-uninstalled" "Render"
if ($uninstall.ExitCode -ne 0) { Add-Failure "low-latency/uninstall: DeviceSelector --uninstall-endpoint on the playback endpoint exited with $($uninstall.ExitCode)" }
if ($eqLeftRender) { Add-Failure "low-latency/uninstall: the playback endpoint's FxProperties still names an EQ APO CLSID" }
Measure-Cable $lowLatencyMeasurements[3] "low-latency" | Out-Null
Copy-Logs "80-low-latency-uninstalled"
[System.IO.File]::WriteAllText($configFile, $config, (New-Object System.Text.UTF8Encoding($false)))
$summary.lowLatency = [pscustomobject]$lowLatency

# ---------------------------------------------------------------------------
Write-Phase "summary"
$summaryPath = Join-Path $SnapshotDirectory "capture-gate.json"
[pscustomobject]$summary | ConvertTo-Json -Depth 6 | Out-File -FilePath $summaryPath -Encoding utf8
$summary.rounds | Format-Table -AutoSize mode, installMode | Out-String | Write-Host
$summary.measurements | Format-Table -AutoSize name, category, raw, period, enginePeriodFrames, expectGainDb, gainDb, passed, required | Out-String | Write-Host
if ($summary.failures.Count -gt 0) {
    Write-Host "capture gate FAILED:"
    $summary.failures | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
Write-Host "capture gate passed: the EQ APO processes the recording endpoint"
exit 0
