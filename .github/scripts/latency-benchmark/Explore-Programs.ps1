<#
.SYNOPSIS
    Temporary: finds out how the benchmark's programs install and start on a
    hosted runner. Prints to the log only. Replaced by the benchmark itself.
#>
param(
    [Parameter(Mandatory)] [string] $Inputs,
    [Parameter(Mandatory)] [string] $Work
)
$ErrorActionPreference = "Continue"
New-Item -ItemType Directory -Force -Path $Work | Out-Null

function Section([string] $title) { Write-Host ""; Write-Host "===== $title =====" }

function Run([string] $file, [string[]] $arguments, [int] $seconds) {
    $out = Join-Path $Work ("run-" + [Guid]::NewGuid().ToString("N") + ".txt")
    $p = Start-Process -FilePath $file -ArgumentList $arguments -PassThru -RedirectStandardOutput $out -RedirectStandardError "$out.err" -WindowStyle Hidden
    if (-not $p.WaitForExit($seconds * 1000)) { try { $p.Kill() } catch {} ; Write-Host "  (still running after $seconds s, killed)" }
    else { Write-Host "  exit $($p.ExitCode)" }
    foreach ($f in $out, "$out.err") { if (Test-Path $f) { Get-Content $f -TotalCount 120 | ForEach-Object { "  | $_" } } }
}

function Tree([string] $path, [int] $depth = 3) {
    if (-not (Test-Path $path)) { Write-Host "  (missing) $path"; return }
    Get-ChildItem -LiteralPath $path -Recurse -Depth $depth -ErrorAction SilentlyContinue |
        ForEach-Object { "  {0,12} {1}" -f $(if ($_.PSIsContainer) { "<dir>" } else { $_.Length }), $_.FullName.Substring($path.Length) }
}

Section "runner"
Get-CimInstance Win32_Processor | ForEach-Object { "  $($_.Name)  cores=$($_.NumberOfCores) logical=$($_.NumberOfLogicalProcessors)" }
"  memory GB: " + [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
"  services: " + ((Get-Service AudioSrv, AudioEndpointBuilder | ForEach-Object { "$($_.Name)=$($_.Status)" }) -join " ")
"  python: " + ((Get-Command python -ErrorAction SilentlyContinue).Source)
"  cargo: " + ((Get-Command cargo -ErrorAction SilentlyContinue).Source)
"  ffmpeg: " + ((Get-Command ffmpeg -ErrorAction SilentlyContinue).Source)

Section "installer kinds"
foreach ($setup in Get-ChildItem -LiteralPath $Inputs -Filter "*Setup.exe") {
    $bytes = [IO.File]::ReadAllBytes($setup.FullName)
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    $kind = @()
    foreach ($mark in "Inno Setup", "Nullsoft", "NSIS", "WiX", "InstallShield", "Squirrel", "JUCE") { if ($text.Contains($mark)) { $kind += $mark } }
    "  $($setup.Name): $($kind -join ', ')"
}

Section "zAuro install"
$before = Get-Date
Run (Join-Path $Inputs "zAuro-0.4.1-Windows-x64-Setup.exe") @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-") 180
Tree "C:\Program Files\zSonic" 3
Tree "C:\Program Files\Common Files\VST3" 4

Section "plugin host install"
Run (Join-Path $Inputs "MultiChannelPluginHost-1.1.1-Windows-x64-Setup.exe") @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-") 180
$hostRoots = @("C:\Program Files", "C:\Program Files (x86)", "$env:LOCALAPPDATA\Programs", "$env:LOCALAPPDATA", "$env:APPDATA") |
    ForEach-Object { Get-ChildItem -LiteralPath $_ -Directory -ErrorAction SilentlyContinue } |
    Where-Object { $_.LastWriteTime -ge $before -or $_.Name -match "MultiChannel|PluginHost" }
foreach ($root in $hostRoots) { "  candidate: $($root.FullName)"; Tree $root.FullName 3 }
$hostExe = $hostRoots | ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Recurse -Filter "*.exe" -ErrorAction SilentlyContinue } |
    Where-Object { $_.Name -notmatch "unins" } | Select-Object -First 3
foreach ($exe in $hostExe) {
    "  --- $($exe.FullName) --help"
    Run $exe.FullName @("--help") 10
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe.FullName))
    "  strings that look like options or settings:"
    [regex]::Matches($text, '(--[a-z][a-z0-9-]{2,30}|[A-Za-z]+\.(xml|json|settings|ini|yaml|yml))') |
        ForEach-Object Value | Sort-Object -Unique | Select-Object -First 80 | ForEach-Object { "    $_" }
}
foreach ($doc in $hostRoots | ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Recurse -Include "*.md", "*.txt" -ErrorAction SilentlyContinue } | Select-Object -First 4) {
    "  --- $($doc.FullName)"
    Get-Content -LiteralPath $doc.FullName -TotalCount 80 | ForEach-Object { "  | $_" }
}

Section "zAuro standalone"
$standalone = "C:\Program Files\zSonic\zAuro\zAuro Pro 2D-3D Upmixer.exe"
if (Test-Path $standalone) {
    Run $standalone @("--help") 10
    foreach ($doc in Get-ChildItem "C:\Program Files\zSonic\zAuro\Notices" -Filter "*.md" -ErrorAction SilentlyContinue) {
        "  --- $($doc.Name)"
        Get-Content -LiteralPath $doc.FullName -TotalCount 60 | ForEach-Object { "  | $_" }
    }
}
"  settings folders created:"
foreach ($root in $env:APPDATA, $env:LOCALAPPDATA) {
    Get-ChildItem -LiteralPath $root -Directory | Where-Object { $_.LastWriteTime -ge $before } | ForEach-Object { "    $($_.FullName)"; Tree $_.FullName 2 }
}

Section "CamillaDSP"
$cdspZip = Join-Path $Work "camilladsp.zip"
Invoke-WebRequest -Uri "https://github.com/HEnquist/camilladsp/releases/download/v4.1.3/camilladsp-windows-amd64.zip" -OutFile $cdspZip
"  sha256 " + (Get-FileHash $cdspZip -Algorithm SHA256).Hash
Expand-Archive -LiteralPath $cdspZip -DestinationPath (Join-Path $Work "camilladsp") -Force
Tree (Join-Path $Work "camilladsp") 2
$cdsp = Get-ChildItem (Join-Path $Work "camilladsp") -Recurse -Filter "camilladsp.exe" | Select-Object -First 1
if ($cdsp) { Run $cdsp.FullName @("--version") 10; Run $cdsp.FullName @("--help") 10 }

Section "Omniphony"
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/mgth/Omniphony/releases/tags/v0.7.0" -Headers @{ "User-Agent" = "benchmark" }
$release.assets | ForEach-Object { "  asset {0}  {1} bytes" -f $_.name, $_.size }
$omniAsset = $release.assets | Where-Object { $_.name -match "^omniphony-.*windows.*setup\.exe$" } | Select-Object -First 1
if ($omniAsset) {
    $omniSetup = Join-Path $Work $omniAsset.name
    Invoke-WebRequest -Uri $omniAsset.browser_download_url -OutFile $omniSetup
    "  sha256 " + (Get-FileHash $omniSetup -Algorithm SHA256).Hash
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($omniSetup))
    "  installer kind: " + ((@("Inno Setup", "Nullsoft", "NSIS", "WiX") | Where-Object { $text.Contains($_) }) -join ", ")
    $beforeOmni = Get-Date
    Run $omniSetup @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-", "/S") 180
    Tree "$env:LOCALAPPDATA\Programs\Omniphony" 3
    Tree "$env:ProgramData\omniphony" 2
    $orender = Get-ChildItem "$env:LOCALAPPDATA\Programs" -Recurse -Filter "orender.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($orender) {
        Run $orender.FullName @("--version") 10
        Run $orender.FullName @("--help") 10
        "  bridge libraries beside it:"
        Get-ChildItem $orender.DirectoryName -Recurse -Filter "*bridge*" | ForEach-Object { "    $($_.FullName)" }
    }
}

Section "audio endpoints before any virtual cable"
Get-PnpDevice -Class AudioEndpoint -ErrorAction SilentlyContinue | ForEach-Object { "  $($_.Status) $($_.FriendlyName)" }
Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue | ForEach-Object { "  media: $($_.Status) $($_.FriendlyName)" }
exit 0
