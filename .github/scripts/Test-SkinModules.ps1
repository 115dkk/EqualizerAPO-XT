param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$skinRoot = Join-Path $repositoryRoot 'Editor\skins'
$themeDataPath = Join-Path $skinRoot 'SkinThemeData.cpp'
$editorProjectPath = Join-Path $repositoryRoot 'Editor\Editor.pro'
$resourcePath = Join-Path $repositoryRoot 'Editor\Editor.qrc'
$deviceResourcePath = Join-Path $repositoryRoot 'DeviceSelector\DeviceSelectorSkins.qrc'
$failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure([string] $Message) {
    $script:failures.Add($Message)
    Write-Host "::error::$Message"
}

function Normalize-RelativePath([string] $Path) {
    return $Path.Replace('\', '/').TrimStart([char[]]@('.', '/'))
}

$themeData = Get-Content -LiteralPath $themeDataPath -Raw
$rosterStart = $themeData.IndexOf('const QVector<SkinEntry>& roster()')
$rosterEnd = $themeData.IndexOf('QStringList ids()', $rosterStart)
if ($rosterStart -lt 0 -or $rosterEnd -lt 0) {
    Add-Failure 'Could not locate SkinThemeData::roster().'
    $skinIds = @()
}
else {
    $rosterText = $themeData.Substring($rosterStart, $rosterEnd - $rosterStart)
    $skinIds = @([regex]::Matches(
        $rosterText,
        '\{\s*QStringLiteral\("(?<id>[^"]+)"\),\s*QStringLiteral\("') |
        ForEach-Object { $_.Groups['id'].Value })
}

if ($skinIds.Count -eq 0) {
    Add-Failure 'SkinThemeData::roster() did not yield any skin ids.'
}

$moduleDirectories = @(Get-ChildItem -LiteralPath $skinRoot -Directory | Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName ($_.Name + '.pri'))
} | ForEach-Object Name | Sort-Object)
$rosterDirectories = @($skinIds | Sort-Object)
$directoryDiff = @(Compare-Object $rosterDirectories $moduleDirectories)
if ($directoryDiff.Count -gt 0) {
    Add-Failure ("Skin roster/module folder mismatch: " + (($directoryDiff | ForEach-Object {
        "{0} ({1})" -f $_.InputObject, $_.SideIndicator
    }) -join ', '))
}

foreach ($id in $skinIds) {
    $moduleRoot = Join-Path $skinRoot $id
    $classStem = $id.Substring(0, 1).ToUpperInvariant() + $id.Substring(1) + 'Skin'
    $priPath = Join-Path $moduleRoot ($id + '.pri')
    foreach ($required in @($moduleRoot, $priPath,
            (Join-Path $moduleRoot ($classStem + '.h')),
            (Join-Path $moduleRoot ($classStem + '.cpp')))) {
        if (-not (Test-Path -LiteralPath $required)) {
            Add-Failure "Skin '$id' is missing $required."
        }
    }
    if (-not (Test-Path -LiteralPath $priPath)) {
        continue
    }

    $priText = (Get-Content -LiteralPath $priPath -Raw).Replace('\', '/')
    $implementationFiles = @(Get-ChildItem -LiteralPath $moduleRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.cpp', '.h') })
    foreach ($file in $implementationFiles) {
        $relative = Normalize-RelativePath($file.FullName.Substring($moduleRoot.Length))
        if ($priText -notmatch [regex]::Escape('$$PWD/' + $relative)) {
            Add-Failure "$relative exists in skin '$id' but is not registered in $id.pri."
        }
    }

    $registeredPaths = @([regex]::Matches($priText, '\$\$PWD/(?<path>[^\s\\]+\.(?:cpp|h))') |
        ForEach-Object { $_.Groups['path'].Value } | Sort-Object -Unique)
    foreach ($registered in $registeredPaths) {
        if (-not (Test-Path -LiteralPath (Join-Path $moduleRoot $registered))) {
            Add-Failure "$id.pri registers missing file '$registered'."
        }
    }

    foreach ($file in $implementationFiles) {
        foreach ($match in [regex]::Matches(
                (Get-Content -LiteralPath $file.FullName -Raw),
                '#\s*include\s*[<"](?<target>[^>"]+)[>"]')) {
            $target = $match.Groups['target'].Value.Replace('\', '/')
            foreach ($otherId in $skinIds) {
                if ($otherId -ne $id -and $target -match "(^|/)$([regex]::Escape($otherId))/") {
                    Add-Failure "$($file.FullName) directly includes skin '$otherId' via '$target'."
                }
            }
        }
    }

    foreach ($source in @(Get-ChildItem -LiteralPath $moduleRoot -Recurse -Filter '*.cpp')) {
        $codeLines = @(Get-Content -LiteralPath $source.FullName | Where-Object {
            $trimmed = $_.Trim()
            $trimmed.Length -gt 0 -and
                -not $trimmed.StartsWith('//') -and
                -not $trimmed.StartsWith('/*') -and
                -not $trimmed.StartsWith('*') -and
                -not $trimmed.StartsWith('*/')
        }).Count
        $limit = if ($source.Name -eq ($classStem + '.cpp')) { 350 } else { 900 }
        if ($codeLines -gt $limit) {
            Add-Failure "$($source.FullName) has $codeLines non-comment lines; limit is $limit."
        }
    }
}

$sharedRoot = Join-Path $skinRoot 'shared'
if (Test-Path -LiteralPath $sharedRoot) {
    foreach ($file in @(Get-ChildItem -LiteralPath $sharedRoot -Recurse -File |
            Where-Object { $_.Extension -in @('.cpp', '.h') })) {
        foreach ($match in [regex]::Matches(
                (Get-Content -LiteralPath $file.FullName -Raw),
                '#\s*include\s*[<"](?<target>[^>"]+)[>"]')) {
            $target = $match.Groups['target'].Value.Replace('\', '/')
            foreach ($id in $skinIds) {
                if ($target -match "(^|/)$([regex]::Escape($id))/") {
                    Add-Failure "$($file.FullName) includes concrete skin '$id'."
                }
            }
        }
    }
}

$skinLiteralPattern = '(?:QStringLiteral\s*\(\s*)?"(' + (($skinIds | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')"'
$sharedSearchRoots = @($sharedRoot, (Join-Path $repositoryRoot 'Editor\widgets')) |
    Where-Object { Test-Path -LiteralPath $_ }
foreach ($root in $sharedSearchRoots) {
    foreach ($file in @(Get-ChildItem -LiteralPath $root -Recurse -File |
            Where-Object { $_.Extension -in @('.cpp', '.h') })) {
        if ((Get-Content -LiteralPath $file.FullName -Raw) -match $skinLiteralPattern) {
            Add-Failure "$($file.FullName) branches on a concrete skin id; keep that decision behind ISkin."
        }
    }
}

$obsoleteFiles = @(
    'StudioSkin.cpp', 'MinimalSkin.cpp', 'SoftSkin.cpp', 'RackSkin.cpp',
    'MatrixSkin.cpp', 'RackChrome.cpp', 'RackChrome.h'
)
foreach ($file in $obsoleteFiles) {
    $oldPath = Join-Path $skinRoot $file
    if (Test-Path -LiteralPath $oldPath) {
        Add-Failure "Obsolete skin implementation remains at $oldPath."
    }
}
foreach ($oldDirectory in @('cards', 'pickers')) {
    $path = Join-Path $skinRoot $oldDirectory
    if (Test-Path -LiteralPath $path) {
        $remaining = @(Get-ChildItem -LiteralPath $path -File -Recurse)
        if ($remaining.Count -gt 0) {
            Add-Failure "Old concrete skin directory '$path' still contains files."
        }
    }
}

$editorProject = (Get-Content -LiteralPath $editorProjectPath -Raw).Replace('\', '/')
if (Test-Path -LiteralPath $sharedRoot) {
    foreach ($file in @(Get-ChildItem -LiteralPath $sharedRoot -Recurse -File |
            Where-Object { $_.Extension -in @('.cpp', '.h') })) {
        $relative = Normalize-RelativePath($file.FullName.Substring($skinRoot.Length))
        if ($editorProject -notmatch [regex]::Escape('skins/' + $relative)) {
            Add-Failure "Shared skin file '$relative' is not registered in Editor.pro."
        }
    }
}
foreach ($id in $skinIds) {
    $expectedInclude = "include(skins/$id/$id.pri)"
    if ($editorProject -notmatch [regex]::Escape($expectedInclude)) {
        Add-Failure "Editor.pro does not include $expectedInclude."
    }
}
if ($editorProject -match 'skins/(?:Studio|Minimal|Soft|Rack|Matrix)Skin\.cpp|skins/(?:cards|pickers)/|widgets/routing/(?:LightTrace|StepList|BlockChip|HardwarePatchbay|CrosspointMatrix)RoutingRenderer') {
    Add-Failure 'Editor.pro still registers concrete skin sources outside the five .pri modules.'
}

$resourceText = (Get-Content -LiteralPath $resourcePath -Raw).Replace('\', '/')
$deviceResourceText = (Get-Content -LiteralPath $deviceResourcePath -Raw).Replace('\', '/')
$qssAliases = @{
    studio = @('studio_dark.qss', 'studio_light.qss')
    minimal = @('precision_dark.qss', 'precision_light.qss')
    soft = @('soft_dark.qss', 'soft_light.qss')
    rack = @('rack_dark.qss', 'rack_light.qss')
    matrix = @('matrix_dark.qss', 'matrix_light.qss')
}
foreach ($id in $qssAliases.Keys) {
    foreach ($qss in $qssAliases[$id]) {
        $entry = "<file alias=`"skins/$qss`">skins/$id/qss/$qss</file>"
        if ($resourceText -notmatch [regex]::Escape($entry)) {
            Add-Failure "Editor.qrc must preserve :/skins/$qss through '$entry'."
        }
        $deviceEntry = "<file alias=`"skins/$qss`">../Editor/skins/$id/qss/$qss</file>"
        if ($deviceResourceText -notmatch [regex]::Escape($deviceEntry)) {
            Add-Failure "DeviceSelectorSkins.qrc must preserve :/skins/$qss through '$deviceEntry'."
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Skin module checks failed: $($failures.Count)"
    exit 1
}

Write-Host "Skin module checks passed for $($skinIds.Count) skins: $($skinIds -join ', ')"
