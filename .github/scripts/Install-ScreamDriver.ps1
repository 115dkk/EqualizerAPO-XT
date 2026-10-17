<#
.SYNOPSIS
    Installs the Scream virtual audio driver on a GitHub-hosted runner so it
    has a real, active render endpoint.

.DESCRIPTION
    Audit #422 TD-48: vst3-preview-probe.yml and audio-live-repro.yml each
    carried the same 70 lines of this recipe.

    Hosted runners have Secure Boot off, so a self-signed kernel driver is
    accepted with no test-signing mode and no reboot. The recipe:
      - download the pinned Scream release and check its SHA-256,
      - create a self-signed code-signing certificate,
      - sign the existing catalog with it (the catalog's hashes of
        Scream.sys and Scream.inf stay valid; only a signature that chains
        to a trusted root is added),
      - trust the certificate in LocalMachine\Root and \TrustedPublisher,
      - install the INF with the bundled devcon,
      - start the audio services, restart AudioEndpointBuilder so the new
        device is enumerated, and wait for a Scream render endpoint to
        appear in MMDevices.

    signtool.exe must be on PATH. The MMDevices root and the friendly-name
    property come from the shared APO endpoint contract the workflows load
    (MMDEVICES_ROOT, DEVICE_FRIENDLY_VALUE).

    The script ends with an explicit exit so a native tool's exit code can
    never decide the calling step through the "exit $LASTEXITCODE" GitHub
    appends to pwsh steps.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Url,
    [Parameter(Mandatory)] [string] $Sha256,
    [Parameter(Mandatory)] [string] $WorkDir,
    [Parameter(Mandatory)] [string] $MmDevicesRoot,
    [Parameter(Mandatory)] [string] $FriendlyNameValue,
    # When set, every line also goes to this file, followed by a device
    # listing for the snapshot artifacts.
    [string] $LogPath
)

$ErrorActionPreference = "Stop"

function Write-Log([string] $message) {
    $line = "[scream] $message"
    Write-Host $line
    if ($LogPath) { Add-Content -Path $LogPath -Value $line }
}

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$zip = Join-Path $WorkDir "Scream.zip"
Write-Log "Downloading $Url"
Invoke-WebRequest -Uri $Url -OutFile $zip
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if ($hash -ne $Sha256) { throw "Scream download hash $hash does not match the pinned $Sha256" }
Expand-Archive -Path $zip -DestinationPath $WorkDir -Force

# Scream4.0.zip extracts to <work>\Install\...; the x64 catalog is the
# lower-case scream.cat that Scream.inf names.
$driverDir = Join-Path $WorkDir "Install\driver\x64"
$inf = Join-Path $driverDir "Scream.inf"
$cat = Join-Path $driverDir "scream.cat"
$devcon = Join-Path $WorkDir "Install\helpers\devcon-x64.exe"
foreach ($f in @($inf, $cat, $devcon)) {
    if (-not (Test-Path $f)) { throw "Scream layout unexpected: $f not found" }
}

Write-Log "Creating a self-signed code-signing certificate"
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject "CN=EqualizerAPO-XT Scream Test Signing" `
    -CertStoreLocation Cert:\CurrentUser\My `
    -KeyUsage DigitalSignature -KeyExportPolicy Exportable
$cer = Join-Path $WorkDir "ScreamTestSigning.cer"
Export-Certificate -Cert $cert -FilePath $cer | Out-Null

Write-Log "Signing $cat with thumbprint $($cert.Thumbprint)"
& signtool sign /v /fd SHA256 /sha1 $cert.Thumbprint $cat
if ($LASTEXITCODE -ne 0) { throw "signtool sign failed ($LASTEXITCODE)" }

Write-Log "Trusting the certificate in LocalMachine\Root and LocalMachine\TrustedPublisher"
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null

# Install-x64.bat does exactly this: cd driver\x64; devcon install
# Scream.inf *Scream (devcon finds the catalog beside the INF).
Write-Log "devcon install Scream.inf *Scream (cwd=$driverDir)"
Push-Location $driverDir
try {
    & $devcon install "Scream.inf" "*Scream"
    $devconRc = $LASTEXITCODE
} finally {
    Pop-Location
}
Write-Log "devcon exited with $devconRc"
# 0 = installed, 1 = installed and a reboot is suggested.
if ($devconRc -ne 0 -and $devconRc -ne 1) { throw "devcon install failed (rc=$devconRc)" }

# Start-Service, not "net start": net exits non-zero when the service is
# already running (windows-2025 starts AudioSrv, windows-2022 does not).
# The AudioEndpointBuilder restart is setup here, so the new device joins
# the live endpoint graph.
Write-Log "Starting the audio services and restarting AudioEndpointBuilder"
try { Start-Service AudioEndpointBuilder -ErrorAction Stop } catch { Write-Log "Start AudioEndpointBuilder: $($_.Exception.Message)" }
try { Start-Service AudioSrv -ErrorAction Stop } catch { Write-Log "Start AudioSrv: $($_.Exception.Message)" }
try { Restart-Service -Force AudioEndpointBuilder -ErrorAction Stop } catch { Write-Log "Restart AudioEndpointBuilder: $($_.Exception.Message)" }
try { Start-Service AudioSrv -ErrorAction Stop } catch { Write-Log "Start AudioSrv: $($_.Exception.Message)" }

$deadline = (Get-Date).AddSeconds(30)
$screamSeen = $false
do {
    Start-Sleep -Milliseconds 1500
    $render = @()
    if (Test-Path "Registry::$MmDevicesRoot\Render") {
        $render = Get-ChildItem "Registry::$MmDevicesRoot\Render" -ErrorAction SilentlyContinue
    }
    foreach ($k in $render) {
        $propPath = "$($k.PSPath)\Properties"
        if (Test-Path $propPath) {
            $name = (Get-ItemProperty -Path $propPath -Name $FriendlyNameValue -ErrorAction SilentlyContinue)."$FriendlyNameValue"
            if ($name -and $name -match 'Scream') { $screamSeen = $true; break }
        }
    }
} while (-not $screamSeen -and (Get-Date) -lt $deadline)
Write-Log "Scream endpoint visible in MMDevices: $screamSeen"

if ($LogPath) {
    # Forensic listing for the snapshot artifacts; never fails the step.
    Write-Log "Win32_SoundDevice:"
    try {
        Get-CimInstance Win32_SoundDevice -ErrorAction Stop | Select-Object Name, Status, StatusInfo |
            Format-Table -AutoSize | Out-String | ForEach-Object { Add-Content -Path $LogPath -Value $_ }
    } catch { Write-Log "Get-CimInstance failed: $($_.Exception.Message)" }
    Write-Log "Get-PnpDevice -Class AudioEndpoint:"
    try {
        Get-PnpDevice -Class AudioEndpoint -ErrorAction Stop | Select-Object Status, FriendlyName, InstanceId |
            Format-Table -AutoSize | Out-String | ForEach-Object { Add-Content -Path $LogPath -Value $_ }
    } catch { Write-Log "Get-PnpDevice failed: $($_.Exception.Message)" }
}

if (-not $screamSeen) { throw "Scream endpoint did not appear in MMDevices within 30 s" }
exit 0
