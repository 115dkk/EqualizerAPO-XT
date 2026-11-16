BeforeAll {
    $script:gate = (Resolve-Path (Join-Path $PSScriptRoot '../Test-SkinModules.ps1')).Path
    $script:repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
    $script:pwsh = (Get-Process -Id $PID).Path
    function Write-FixtureFile([string] $relative, [string] $text) {
        $path = Join-Path $script:root $relative
        New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force | Out-Null
        [IO.File]::WriteAllText($path, $text + "`n")
    }
    function Invoke-SkinFixture {
        $lines = & $script:pwsh -NoProfile -File $script:gate -RepositoryRoot $script:root 2>&1
        [pscustomobject]@{ Exit = $LASTEXITCODE; Text = $lines -join "`n" }
    }
    function New-SkinFixture {
        $script:root = Join-Path $TestDrive ([guid]::NewGuid().ToString('N'))
        $ids = @('studio', 'minimal', 'soft', 'rack', 'matrix')
        $roster = 'const QVector<SkinEntry>& roster() {'
        $project = ''; $editorQrc = ''; $deviceQrc = ''
        foreach ($id in $ids) {
            $stem = $id.Substring(0, 1).ToUpperInvariant() + $id.Substring(1) + 'Skin'
            $roster += '{ QStringLiteral("' + $id + '"), QStringLiteral("' + $id + '") },'
            Write-FixtureFile "Editor/skins/$id/$stem.h" '// header'
            Write-FixtureFile "Editor/skins/$id/$stem.cpp" '// source'
            Write-FixtureFile "Editor/skins/$id/$id.pri" ('HEADERS += $$PWD/' + $stem + '.h' + "`nSOURCES += " + '$$PWD/' + $stem + '.cpp')
            $project += "include(skins/$id/$id.pri)`n"
            $prefix = if ($id -eq 'minimal') { 'precision' } else { $id }
            foreach ($mode in @('dark', 'light')) {
                $qss = "${prefix}_${mode}.qss"
                $editorQrc += "<file alias=`"skins/$qss`">skins/$id/qss/$qss</file>`n"
                $deviceQrc += "<file alias=`"skins/$qss`">../Editor/skins/$id/qss/$qss</file>`n"
            }
        }
        Write-FixtureFile 'Editor/skins/SkinThemeData.cpp' ($roster + '} QStringList ids()')
        Write-FixtureFile 'Editor/Editor.pro' $project
        Write-FixtureFile 'Editor/Editor.qrc' $editorQrc
        Write-FixtureFile 'DeviceSelector/DeviceSelectorSkins.qrc' $deviceQrc
    }
}

Describe 'Skin module gate on fixture trees' {
    BeforeEach { New-SkinFixture }
    It 'accepts the minimal valid fixture' {
        $r = Invoke-SkinFixture
        $r.Exit | Should -Be 0
        $r.Text | Should -Match 'Skin module checks passed for 5 skins'
    }
    It 'rejects <Rule>' -ForEach @(
        @{ Rule = 'missing roster'; Change = { Write-FixtureFile 'Editor/skins/SkinThemeData.cpp' '// no roster' }; Message = 'Could not locate SkinThemeData::roster' }
        @{ Rule = 'empty roster'; Change = { Write-FixtureFile 'Editor/skins/SkinThemeData.cpp' 'const QVector<SkinEntry>& roster() {} QStringList ids()' }; Message = 'did not yield any skin ids' }
        @{ Rule = 'module mismatch'; Change = { Write-FixtureFile 'Editor/skins/extra/extra.pri' '// extra' }; Message = 'roster/module folder mismatch' }
        @{ Rule = 'missing folder'; Change = { Remove-Item (Join-Path $script:root 'Editor/skins/studio') -Recurse }; Message = "Skin 'studio' is missing" }
        @{ Rule = 'missing pri'; Change = { Remove-Item (Join-Path $script:root 'Editor/skins/studio/studio.pri') }; Message = 'is missing.*studio.pri' }
        @{ Rule = 'missing header'; Change = { Remove-Item (Join-Path $script:root 'Editor/skins/studio/StudioSkin.h') }; Message = 'is missing.*StudioSkin.h' }
        @{ Rule = 'missing source'; Change = { Remove-Item (Join-Path $script:root 'Editor/skins/studio/StudioSkin.cpp') }; Message = 'is missing.*StudioSkin.cpp' }
        @{ Rule = 'unregistered source'; Change = { Write-FixtureFile 'Editor/skins/studio/extra.cpp' '// extra' }; Message = 'extra.cpp exists.*not registered' }
        @{ Rule = 'dangling pri entry'; Change = { Add-Content (Join-Path $script:root 'Editor/skins/studio/studio.pri') '$$PWD/missing.h' }; Message = 'registers missing file' }
        @{ Rule = 'cross skin include'; Change = { Write-FixtureFile 'Editor/skins/studio/StudioSkin.cpp' '#include "../rack/RackSkin.h"' }; Message = 'directly includes skin' }
        @{ Rule = 'main source line limit'; Change = { Write-FixtureFile 'Editor/skins/studio/StudioSkin.cpp' ((1..351 | ForEach-Object { 'int line;' }) -join "`n") }; Message = '351 non-comment lines; limit is 350' }
        @{ Rule = 'part source line limit'; Change = { Write-FixtureFile 'Editor/skins/studio/part.cpp' ((1..901 | ForEach-Object { 'int line;' }) -join "`n"); Add-Content (Join-Path $script:root 'Editor/skins/studio/studio.pri') '$$PWD/part.cpp' }; Message = '901 non-comment lines; limit is 900' }
        @{ Rule = 'shared concrete include'; Change = { Write-FixtureFile 'Editor/skins/shared/common.h' '#include "../rack/RackSkin.h"' }; Message = 'includes concrete skin' }
        @{ Rule = 'shared concrete id'; Change = { Write-FixtureFile 'Editor/skins/shared/common.cpp' 'QStringLiteral("rack")' }; Message = 'branches on a concrete skin id' }
        @{ Rule = 'widget concrete id'; Change = { Write-FixtureFile 'Editor/widgets/widget.h' '"studio"' }; Message = 'branches on a concrete skin id' }
        @{ Rule = 'obsolete source'; Change = { Write-FixtureFile 'Editor/skins/RackChrome.cpp' '// old' }; Message = 'Obsolete skin implementation' }
        @{ Rule = 'obsolete cards directory'; Change = { Write-FixtureFile 'Editor/skins/cards/old.cpp' '// old' }; Message = 'Old concrete skin directory' }
        @{ Rule = 'obsolete pickers directory'; Change = { Write-FixtureFile 'Editor/skins/pickers/old.cpp' '// old' }; Message = 'Old concrete skin directory' }
        @{ Rule = 'unregistered shared source'; Change = { Write-FixtureFile 'Editor/skins/shared/common.h' '// shared' }; Message = 'Shared skin file.*not registered' }
        @{ Rule = 'missing module include'; Change = { Write-FixtureFile 'Editor/Editor.pro' '// empty project' }; Message = 'Editor.pro does not include' }
        @{ Rule = 'legacy concrete registration'; Change = { Add-Content (Join-Path $script:root 'Editor/Editor.pro') 'skins/StudioSkin.cpp' }; Message = 'still registers concrete skin sources' }
        @{ Rule = 'legacy routing registration'; Change = { Add-Content (Join-Path $script:root 'Editor/Editor.pro') 'widgets/routing/LightTraceRoutingRenderer.cpp' }; Message = 'still registers concrete skin sources' }
        @{ Rule = 'Editor resource alias'; Change = { Write-FixtureFile 'Editor/Editor.qrc' '<RCC/>' }; Message = 'Editor.qrc must preserve' }
        @{ Rule = 'DeviceSelector resource alias'; Change = { Write-FixtureFile 'DeviceSelector/DeviceSelectorSkins.qrc' '<RCC/>' }; Message = 'DeviceSelectorSkins.qrc must preserve' }
    ) {
        & $Change
        $r = Invoke-SkinFixture
        $r.Exit | Should -Be 1
        $r.Text | Should -Match $Message
        $r.Text | Should -Match 'Skin module checks failed:'
    }
    It 'accepts exact line limits and excludes blank and comment-only lines' {
        Write-FixtureFile 'Editor/skins/studio/StudioSkin.cpp' (((1..350 | ForEach-Object { 'int line;' }) + @('', '// comment', '/* comment', '* comment', '*/')) -join "`n")
        Write-FixtureFile 'Editor/skins/studio/part.cpp' ((1..900 | ForEach-Object { 'int line;' }) -join "`n")
        Add-Content (Join-Path $script:root 'Editor/skins/studio/studio.pri') '$$PWD/part.cpp'
        (Invoke-SkinFixture).Exit | Should -Be 0
    }
    It 'accepts registered shared files, empty old folders and non-concrete includes' {
        Write-FixtureFile 'Editor/skins/shared/common.h' '#include "../ISkin.h"'
        Add-Content (Join-Path $script:root 'Editor/Editor.pro') 'skins/shared/common.h'
        New-Item -ItemType Directory -Path (Join-Path $script:root 'Editor/skins/cards') | Out-Null
        (Invoke-SkinFixture).Exit | Should -Be 0
    }
}

Describe 'Skin module gate on the repository' {
    It 'passes the real tree' {
        $script:root = $script:repo
        $r = Invoke-SkinFixture
        $r.Exit | Should -Be 0
        $r.Text | Should -Match 'Skin module checks passed for 5 skins'
    }
}
