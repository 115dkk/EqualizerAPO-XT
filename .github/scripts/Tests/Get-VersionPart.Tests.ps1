Describe "Get-VersionPart" {
    # Audit #275 TD-33 follow-up: the canonical version.h parser feeds both the
    # version-bump job and create-release, but had no direct test.
    BeforeAll {
        . (Join-Path $PSScriptRoot "..\Get-VersionPart.ps1")
        $script:sampleLines = @(
            '#pragma once',
            '#define MAJOR 2',
            '#define MINOR 34',
            '  #define REVISION 4',
            '#define VERSION_STR "2.34.4"'
        )
    }

    It "reads each numeric component from provided lines" {
        Get-VersionPart -Name "MAJOR" -Lines $sampleLines | Should -Be 2
        Get-VersionPart -Name "MINOR" -Lines $sampleLines | Should -Be 34
        Get-VersionPart -Name "REVISION" -Lines $sampleLines | Should -Be 4
    }

    It "throws on a missing component instead of returning a default" {
        { Get-VersionPart -Name "PATCH" -Lines $sampleLines } | Should -Throw "*PATCH*"
    }

    It "parses the repository's actual version.h" {
        $lines = Get-Content (Join-Path $PSScriptRoot "..\..\..\version.h")
        Get-VersionPart -Name "MAJOR" -Lines $lines | Should -BeGreaterOrEqual 1
        Get-VersionPart -Name "MINOR" -Lines $lines | Should -BeGreaterOrEqual 0
        Get-VersionPart -Name "REVISION" -Lines $lines | Should -BeGreaterOrEqual 0
    }
}
