# Filesystem collection is separate from the pure verdict (audit #467 TD-67).
# Snapshot values contain only strings and arrays; no lazy FileInfo properties.
Set-StrictMode -Version Latest

function Get-SkinModuleMeasurements([string] $RepositoryRoot) {
    $files = @{}
    $directories = @{}
    foreach ($relative in @('Editor/skins', 'Editor/widgets')) {
        $root = Join-Path $RepositoryRoot $relative
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $items = @((Get-Item -LiteralPath $root)) + @(Get-ChildItem -LiteralPath $root -Recurse)
        foreach ($item in $items) {
            if ($item.PSIsContainer) {
                $directories[$item.FullName] = [pscustomobject]@{ FullName = $item.FullName; Name = $item.Name; Parent = $item.Parent.FullName }
            } else {
                # Only implementation, module and sheet text is consumed by the verdict.
                $text = if ($item.Extension -in @('.cpp', '.h', '.pri', '.qss')) { [IO.File]::ReadAllText($item.FullName) } else { '' }
                $files[$item.FullName] = [pscustomobject]@{ FullName = $item.FullName; Name = $item.Name; Extension = $item.Extension; Parent = $item.DirectoryName; Text = $text }
            }
        }
    }
    foreach ($relative in @('Editor/Editor.pro', 'Editor/Editor.qrc', 'DeviceSelector/DeviceSelectorSkins.qrc')) {
        $path = Join-Path $RepositoryRoot $relative
        $files[$path] = [pscustomobject]@{ FullName = $path; Name = [IO.Path]::GetFileName($path); Extension = [IO.Path]::GetExtension($path); Parent = [IO.Path]::GetDirectoryName($path); Text = [IO.File]::ReadAllText($path) }
    }
    [pscustomobject]@{ Root = $RepositoryRoot; Files = $files; Directories = $directories }
}

function Get-SkinModuleVerdict($Measurements) {
    $repositoryRoot = $Measurements.Root
    function Test-SnapshotPath([string] $Path) {
        $Measurements.Files.ContainsKey($Path) -or $Measurements.Directories.ContainsKey($Path)
    }
    function Get-SnapshotText([string] $Path) { $Measurements.Files[$Path].Text }
    function Get-SnapshotChildren([string] $Path, [switch] $Directory, [switch] $File, [switch] $Recurse, [string] $Filter = '*') {
        $items = if ($Directory) { @($Measurements.Directories.Values) } elseif ($File) { @($Measurements.Files.Values) } else { @($Measurements.Directories.Values) + @($Measurements.Files.Values) }
        $prefix = $Path.TrimEnd([char[]]@([IO.Path]::DirectorySeparatorChar, '/')) + [IO.Path]::DirectorySeparatorChar
        $items | Where-Object {
            $under = if ($Recurse) { $_.FullName.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) } else { $_.Parent -eq $Path }
            $under -and $_.Name -like $Filter
        } | Sort-Object FullName
    }

    $skinRoot = Join-Path $repositoryRoot 'Editor\skins'
    $themeDataPath = Join-Path $skinRoot 'SkinThemeData.cpp'
    $editorProjectPath = Join-Path $repositoryRoot 'Editor\Editor.pro'
    $resourcePath = Join-Path $repositoryRoot 'Editor\Editor.qrc'
    $deviceResourcePath = Join-Path $repositoryRoot 'DeviceSelector\DeviceSelectorSkins.qrc'
    $failures = [System.Collections.Generic.List[string]]::new()

    function Add-Failure([string] $Message) {
        $failures.Add($Message)
    }

    function Normalize-RelativePath([string] $Path) {
        return $Path.Replace('\', '/').TrimStart([char[]]@('.', '/'))
    }

    $themeData = Get-SnapshotText $themeDataPath
    $rosterStart = $themeData.IndexOf('const QVector<SkinEntry>& roster()')
    $rosterEnd = if ($rosterStart -ge 0) { $themeData.IndexOf('QStringList ids()', $rosterStart) } else { -1 }
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

    $moduleDirectories = @(Get-SnapshotChildren $skinRoot -Directory | Where-Object {
        Test-SnapshotPath (Join-Path $_.FullName ($_.Name + '.pri'))
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
            if (-not (Test-SnapshotPath $required)) {
                Add-Failure "Skin '$id' is missing $required."
            }
        }
        if (-not (Test-SnapshotPath $priPath)) {
            continue
        }

        $priText = (Get-SnapshotText $priPath).Replace('\', '/')
        $implementationFiles = @(Get-SnapshotChildren $moduleRoot -Recurse -File |
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
            if (-not (Test-SnapshotPath (Join-Path $moduleRoot $registered))) {
                Add-Failure "$id.pri registers missing file '$registered'."
            }
        }

        foreach ($file in $implementationFiles) {
            foreach ($match in [regex]::Matches(
                    (Get-SnapshotText $file.FullName),
                    '#\s*include\s*[<"](?<target>[^>"]+)[>"]')) {
                $target = $match.Groups['target'].Value.Replace('\', '/')
                foreach ($otherId in $skinIds) {
                    if ($otherId -ne $id -and $target -match "(^|/)$([regex]::Escape($otherId))/") {
                        Add-Failure "$($file.FullName) directly includes skin '$otherId' via '$target'."
                    }
                }
            }
        }

        foreach ($source in @(Get-SnapshotChildren $moduleRoot -Recurse -Filter '*.cpp')) {
            $codeLines = @((Get-SnapshotText $source.FullName) -split '\r?\n' | Where-Object {
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
    if (Test-SnapshotPath $sharedRoot) {
        foreach ($file in @(Get-SnapshotChildren $sharedRoot -Recurse -File |
                Where-Object { $_.Extension -in @('.cpp', '.h') })) {
            foreach ($match in [regex]::Matches(
                    (Get-SnapshotText $file.FullName),
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
        Where-Object { Test-SnapshotPath $_ }
    foreach ($root in $sharedSearchRoots) {
        foreach ($file in @(Get-SnapshotChildren $root -Recurse -File |
                Where-Object { $_.Extension -in @('.cpp', '.h') })) {
            if ((Get-SnapshotText $file.FullName) -match $skinLiteralPattern) {
                Add-Failure "$($file.FullName) branches on a concrete skin id; keep that decision behind ISkin."
            }
        }
    }

    $obsoleteFiles = @(
        'StudioSkin.cpp', 'MinimalSkin.cpp', 'FlatSkin.cpp', 'RackSkin.cpp',
        'MatrixSkin.cpp', 'RackChrome.cpp', 'RackChrome.h'
    )
    foreach ($file in $obsoleteFiles) {
        $oldPath = Join-Path $skinRoot $file
        if (Test-SnapshotPath $oldPath) {
            Add-Failure "Obsolete skin implementation remains at $oldPath."
        }
    }
    foreach ($oldDirectory in @('cards', 'pickers')) {
        $path = Join-Path $skinRoot $oldDirectory
        if (Test-SnapshotPath $path) {
            $remaining = @(Get-SnapshotChildren $path -File -Recurse)
            if ($remaining.Count -gt 0) {
                Add-Failure "Old concrete skin directory '$path' still contains files."
            }
        }
    }

    $editorProject = (Get-SnapshotText $editorProjectPath).Replace('\', '/')
    if (Test-SnapshotPath $sharedRoot) {
        foreach ($file in @(Get-SnapshotChildren $sharedRoot -Recurse -File |
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
    if ($editorProject -match 'skins/(?:Studio|Minimal|Flat|Rack|Matrix)Skin\.cpp|skins/(?:cards|pickers)/|widgets/routing/(?:LightTrace|StepList|BlockChip|HardwarePatchbay|CrosspointMatrix)RoutingRenderer') {
        Add-Failure 'Editor.pro still registers concrete skin sources outside the five .pri modules.'
    }

    $resourceText = (Get-SnapshotText $resourcePath).Replace('\', '/')
    $deviceResourceText = (Get-SnapshotText $deviceResourcePath).Replace('\', '/')
    $qssAliases = @{
        studio = @('studio_dark.qss', 'studio_light.qss')
        minimal = @('precision_dark.qss', 'precision_light.qss')
        flat = @('flat_dark.qss', 'flat_light.qss')
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

    # Every image a sheet names must be compiled into both executables. The
    # Device Selector loads the Editor's sheets through its own resource list,
    # and a url() Qt cannot resolve draws nothing and reports nothing: through
    # v2.69.0 its check boxes had no check mark in four skins because
    # check-light.svg and check-dark.svg were listed in Editor.qrc only.
    foreach ($sheet in @(Get-SnapshotChildren $skinRoot -File -Recurse -Filter '*.qss')) {
        $images = [regex]::Matches($sheet.Text, 'url\(:/([^)]+)\)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
        foreach ($image in $images) {
            if ($resourceText -notmatch ('[">]' + [regex]::Escape($image) + '[<"]')) {
                Add-Failure "Editor.qrc does not carry :/$image, which $($sheet.Name) names."
            }
            $deviceEntry = "<file alias=`"$image`">../Editor/$image</file>"
            if ($deviceResourceText -notmatch [regex]::Escape($deviceEntry)) {
                Add-Failure "DeviceSelectorSkins.qrc does not carry :/$image, which $($sheet.Name) names; add '$deviceEntry'."
            }
        }
    }

    [pscustomobject]@{ Passed = $failures.Count -eq 0; Reasons = @($failures.ToArray()); SkinIds = $skinIds }
}

Export-ModuleMember -Function Get-SkinModuleMeasurements, Get-SkinModuleVerdict
