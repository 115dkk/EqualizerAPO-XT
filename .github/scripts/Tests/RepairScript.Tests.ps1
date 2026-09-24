# tools/Repair-EqualizerAPO.ps1 re-applies the ACLs the install hook sets.
# It once granted Users Full control on the config directory after the product
# had lowered that grant to Modify (audit #250 F043), undoing a security
# decision through the support path (audit #348 TD-17). This pins the script's
# grant for the Users SID to the one services/security/AudioEngineAccess.cpp
# uses.
Describe "repair script ACL grants" {
    BeforeAll {
        $repoRoot = Join-Path $PSScriptRoot "..\..\.."
        $script = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "tools\Repair-EqualizerAPO.ps1")
        $cpp = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "services\security\AudioEngineAccess.cpp")
    }

    It "grants the config directory to Users with the same right as the product" {
        $body = $cpp.Substring($cpp.IndexOf("Grant grantConfigAccess("))
        $body = $body.Substring(0, $body.IndexOf("return applyGrant"))
        $productMatch = [regex]::Match($body, 'kUsersSid\) \+ L":\(OI\)\(CI\)([A-Z]+) ')
        $productMatch.Success | Should -BeTrue
        $scriptMatch = [regex]::Match($script, "'\*S-1-5-32-545:\(OI\)\(CI\)([A-Z]+)',\s*'\*S-1-5-19:\(OI\)\(CI\)M'")
        $scriptMatch.Success | Should -BeTrue
        $scriptMatch.Groups[1].Value | Should -Be $productMatch.Groups[1].Value
        $scriptMatch.Groups[1].Value | Should -Be "M"
    }
}
