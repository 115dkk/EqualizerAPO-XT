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

Export-ModuleMember -Function Get-ApoEndpointContract, Export-ApoEndpointContract, Get-ApoFxValueNames
