Set-StrictMode -Version Latest

function Get-ApoEndpointContract {
    [CmdletBinding()]
    param()

    [ordered]@{
        EQ_PREMIX_GUID        = "{EACD2258-FCAC-4FF4-B36D-419E924A6D79}"
        EQ_POSTMIX_GUID       = "{EC1CC9CE-FAED-4822-828A-82A81A6F018F}"
        FX_LFX                = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1"
        FX_GFX                = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2"
        FX_SFX                = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5"
        FX_MFX                = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6"
        FX_EFX                = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7"
        DEVICE_FRIENDLY_VALUE = "{b3f8fa53-0004-438e-9003-51a46e139bfc},6"
        MMDEVICES_ROOT        = "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio"
        CHILD_APO_ROOT        = "HKLM\SOFTWARE\EqualizerAPO\Child APOs"
    }
}

function Export-ApoEndpointContract {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $GitHubEnvironmentPath)

    foreach ($entry in (Get-ApoEndpointContract).GetEnumerator()) {
        "$($entry.Key)=$($entry.Value)" |
            Out-File -FilePath $GitHubEnvironmentPath -Append -Encoding utf8
    }
}

function Get-ApoFxValueNames {
    $contract = Get-ApoEndpointContract
    @($contract.FX_LFX, $contract.FX_GFX, $contract.FX_SFX,
        $contract.FX_MFX, $contract.FX_EFX)
}

# The five effect slots by their short names, in install order. The capture
# gate spelled this table out twice (audit #348 F23).
function Get-ApoFxSlots {
    $contract = Get-ApoEndpointContract
    [ordered]@{
        LFX = $contract.FX_LFX
        GFX = $contract.FX_GFX
        SFX = $contract.FX_SFX
        MFX = $contract.FX_MFX
        EFX = $contract.FX_EFX
    }
}

# An endpoint's FxProperties values, or $null when the key is absent.
function Get-ApoFxProperties {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [ValidateSet("Render", "Capture")] [string] $Flow,
        [Parameter(Mandatory)] [string] $EndpointGuid
    )

    $root = (Get-ApoEndpointContract).MMDEVICES_ROOT -replace '^HKLM\\', 'HKEY_LOCAL_MACHINE\'
    Get-ItemProperty -Path "Registry::$root\$Flow\$EndpointGuid\FxProperties" -ErrorAction SilentlyContinue
}

# The effect chain an FxProperties value set names, as "LFX=... GFX=...";
# empty slots are left out.
function Format-ApoEffectChain {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [AllowNull()] $FxProperties)

    if ($null -eq $FxProperties) { return $null }
    $slots = @()
    foreach ($slot in (Get-ApoFxSlots).GetEnumerator()) {
        $property = $FxProperties.PSObject.Properties[$slot.Value]
        if ($property -and $property.Value) { $slots += "$($slot.Key)=$($property.Value)" }
    }
    return ($slots -join " ")
}

# Whether any FxProperties value still names one of the EQ APO's CLSIDs.
function Test-ApoEqClsid {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [AllowNull()] $FxProperties)

    if ($null -eq $FxProperties) { return $false }
    $contract = Get-ApoEndpointContract
    $clsids = @($contract.EQ_PREMIX_GUID, $contract.EQ_POSTMIX_GUID) |
        ForEach-Object { $_.Trim('{', '}') }
    foreach ($property in $FxProperties.PSObject.Properties) {
        foreach ($clsid in $clsids) {
            if ("$($property.Value)" -match [regex]::Escape($clsid)) { return $true }
        }
    }
    return $false
}

# Audit #422 T4: shared by the gate scripts of this family.
# Runs a program, waits (with a deadline), and returns exit code and output.
# GUI-subsystem programs (DeviceSelector) print to the console they attach
# to and to their log; their stdout here is often empty, and their exit code
# is the verdict.
function Invoke-ExternalProgram([string] $file, [string[]] $arguments, [int] $timeoutSeconds, [string] $workingDirectory) {
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

Export-ModuleMember -Function Invoke-ExternalProgram, Get-ApoEndpointContract, Export-ApoEndpointContract, Get-ApoFxValueNames, `
    Get-ApoFxSlots, Get-ApoFxProperties, Format-ApoEffectChain, Test-ApoEqClsid
