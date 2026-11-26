/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ConfigDependencyScanner.h"
#include "ImportFileSystem.h"
#include "../ConfigFileCodec.h"
#include "../widgets/FilterCardModel.h"
#include "filters/ConfigFileReference.h"
#include "filters/MultiConvolutionCommand.h"
#include "filters/VSTPluginCommand.h"
#include "filters/subwooferRouting/SubwooferRoutingCommand.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace EqAPO::Import
{

namespace
{

// keyword is the engine's canonical command (FilterCardModel::canonicalCommand),
// so the scanner follows exactly the lines the engine would follow: a lowercase
// "include:" is prose to the engine and must not drag a file into the import.
bool isReferenceCommand(const QString& keyword)
{
    return keyword == QStringLiteral("Include")
        || keyword == QStringLiteral("Convolution")
        || keyword == QStringLiteral("MultiConvolution")
        || keyword == QStringLiteral("VSTPlugin")
        || keyword == QStringLiteral("SubwooferRouting");
}

// The path portion of the line for the given reference command, as written.
// The convolution family and VSTPlugin share their engine grammar so routing
// factors, mappings, state and parameter pairs never leak into the path;
// quotes and variables are left to ConfigFileReference::resolve, the engine's
// own reading.
QString referencePath(const QString& keyword, const QString& parameters)
{
    if (keyword == QStringLiteral("MultiConvolution"))
    {
        MultiConvolutionCommand command;
        if (!MultiConvolutionCommand::parse(L"MultiConvolution", parameters.toStdWString(), command))
            return QString();
        return QString::fromStdWString(command.path);
    }
    if (keyword == QStringLiteral("Convolution"))
        return parameters;
    if (keyword == QStringLiteral("VSTPlugin"))
        return QString::fromStdWString(
            VSTPluginCommand::extractLibraryReference(parameters.toStdWString()));
    if (keyword == QStringLiteral("SubwooferRouting"))
    {
        // Only the Profile form references a file; inline State is
        // self-contained JSON and must not be treated as a path.
        SubwooferRoutingCommand command;
        if (!SubwooferRoutingCommand::parse(L"SubwooferRouting", parameters.toStdWString(), command)
            || command.form != SubwooferRoutingCommand::Form::Profile)
            return QString();
        return QString::fromStdWString(command.payload);
    }
    return parameters;
}

QString resolveAbsolute(const QString& reference, const QString& configPath)
{
    const std::wstring resolved = ConfigFileReference::resolve(
        configPath.toStdWString(), reference.toStdWString());
    if (resolved.empty())
        return QString();
    return QDir::cleanPath(QString::fromStdWString(resolved));
}

// Returns relativePath using forward slashes if target is inside rootDir,
// or an empty string if it is not. We don't allow ".." segments in the
// destination because that would let an import escape the config tree.
QString relativeToRoot(const QString& targetAbs, const QString& rootDir)
{
    QDir root(rootDir);
    QString rel = root.relativeFilePath(targetAbs);
    if (rel.isEmpty() || rel.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(rel))
        return QString();
    return QDir::fromNativeSeparators(rel);
}

// Destination path for a source-relative path under the chosen layout.
QString destForRelative(const QString& rel, const QString& rootSourceDir, DestLayout layout)
{
    if (layout == DestLayout::SourceFolderIsRoot)
        return rel;
    QString rootName = QFileInfo(rootSourceDir).fileName();
    return rootName.isEmpty() ? rel : rootName + QStringLiteral("/") + rel;
}

void appendItem(ImportManifest& manifest,
                const QString& sourceAbs,
                const QString& destRel,
                const QString& kind)
{
    for (const ImportItem& existing : manifest.items)
    {
        if (existing.sourceAbsolute == sourceAbs)
            return; // already collected
    }

    ImportItem item;
    item.sourceAbsolute = sourceAbs;
    item.destRelative = destRel;
    item.kind = kind;

    QFileInfo info(sourceAbs);
    if (info.exists() && info.isFile())
    {
        item.exists = true;
        item.sizeBytes = info.size();
        manifest.totalBytes += item.sizeBytes;
    }
    else
    {
        item.exists = false;
        manifest.hasErrors = true;
        manifest.warnings.append(ConfigDependencyScanner::tr("Missing file: %1").arg(sourceAbs));
    }

    manifest.items.append(item);
}

void scanConfigFile(ImportManifest& manifest,
                    const QString& sourceTxtAbs,
                    const QString& rootSourceDir,
                    DestLayout layout,
                    int depth,
                    QSet<QString>& visited)
{
    if (depth >= ConfigDependencyScanner::kRecursionLimit)
    {
        manifest.warnings.append(ConfigDependencyScanner::tr("Recursion limit reached at %1; nested references were not followed.").arg(sourceTxtAbs));
        manifest.hasErrors = true;
        return;
    }

    if (visited.contains(sourceTxtAbs))
        return;
    visited.insert(sourceTxtAbs);

    const ConfigFileCodec::ReadResult readResult = ConfigFileCodec::readConfig(sourceTxtAbs);
    if (!readResult.ok)
    {
        manifest.warnings.append(ConfigDependencyScanner::tr("Cannot open %1 for scanning.").arg(sourceTxtAbs));
        manifest.hasErrors = true;
        return;
    }

    for (const QString& line : readResult.lines)
    {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;

        QString parameters;
        QString command = FilterCardModel::commandForLine(line, &parameters);
        const QString keyword = FilterCardModel::canonicalCommand(command);

        if (!isReferenceCommand(keyword))
            continue;

        QString reference = referencePath(keyword, parameters);
        if (keyword == QStringLiteral("VSTPlugin"))
        {
            if (reference.isEmpty())
            {
                manifest.warnings.append(ConfigDependencyScanner::tr(
                    "VSTPlugin line has no Library reference: %1 (in %2)")
                    .arg(parameters, sourceTxtAbs));
                manifest.hasErrors = true;
            }
            else if (!manifest.externalReferences.contains(reference))
                manifest.externalReferences.append(reference);

            // VST libraries remain external: record the Library value for
            // diagnostics, but do not add the binary as a copy item.
            continue;
        }

        QString refAbs = resolveAbsolute(reference, sourceTxtAbs);
        if (refAbs.isEmpty())
            continue;

        QString rel = relativeToRoot(refAbs, rootSourceDir);
        if (rel.isEmpty())
        {
            manifest.warnings.append(ConfigDependencyScanner::tr("Reference outside the source folder will be skipped: %1 (in %2)")
                .arg(parameters, sourceTxtAbs));
            manifest.hasErrors = true;
            continue;
        }

        // The manifest's kind column shows the engine's own keyword, so the
        // import list names each row the way the config line spells it.
        appendItem(manifest, refAbs, destForRelative(rel, rootSourceDir, layout), keyword);

        if (keyword == QStringLiteral("Include"))
            scanConfigFile(manifest, refAbs, rootSourceDir, layout, depth + 1, visited);
    }
}

}

ImportManifest ConfigDependencyScanner::scanPluginLibrary(const QString& library, const QString& configPath)
{
    const QString source = QDir::cleanPath(QFileInfo(library).absoluteFilePath());
    const auto target = ConfigFileReference::library(L"",
        QDir::toNativeSeparators(source).toStdWString(), configPath.toStdWString());
    QString bundle;
    if (target && QFileInfo(QString::fromStdWString(target.path.path())).isFile())
    {
        if (QFileInfo(source).isDir())
            bundle = source;
        else
        {
            QDir candidate = QFileInfo(source).absoluteDir();
            if (candidate.cdUp() && candidate.cdUp())
            {
                const auto bundleTarget = ConfigFileReference::library(L"",
                    QDir::toNativeSeparators(candidate.absolutePath()).toStdWString(), configPath.toStdWString());
                BY_HANDLE_FILE_INFORMATION namedInfo = {};
                BY_HANDLE_FILE_INFORMATION bundleInfo = {};
                if (bundleTarget
                    && GetFileInformationByHandle(target.path.leaf(), &namedInfo)
                    && GetFileInformationByHandle(bundleTarget.path.leaf(), &bundleInfo)
                    && namedInfo.dwVolumeSerialNumber == bundleInfo.dwVolumeSerialNumber
                    && namedInfo.nFileIndexHigh == bundleInfo.nFileIndexHigh
                    && namedInfo.nFileIndexLow == bundleInfo.nFileIndexLow)
                    bundle = candidate.absolutePath();
            }
        }
    }
    if (bundle.isEmpty())
        return scan(library, QFileInfo(configPath).absolutePath());

    ImportManifest manifest;
    manifest.rootSource = source;
    manifest.rootSourceDir = QFileInfo(bundle).absolutePath();
    manifest.rootDest = destForRelative(relativeToRoot(source, manifest.rootSourceDir),
        manifest.rootSourceDir, DestLayout::NestUnderSourceFolder);

    // Enumerate one directory at a time so no link is followed, including
    // junctions and hidden or broken file links. Resources and root metadata
    // belong to the bundle just as much as the module does.
    QStringList pending{bundle};
    while (!pending.isEmpty())
    {
        const QString path = pending.takeLast();
        if (isReparsePoint(path))
        {
            manifest.warnings.append(tr("Link not copied: %1").arg(path));
            continue;
        }
        const QFileInfo info(path);
        if (info.isDir())
        {
            if (!canListDirectory(path))
            {
                manifest.hasErrors = true;
                manifest.warnings.append(tr("Cannot open %1 for scanning.").arg(path));
                continue;
            }
            const QFileInfoList children = QDir(path).entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
            // The stack visits entries in the same stable order as the listing.
            for (auto it = children.crbegin(); it != children.crend(); ++it)
                pending.append(it->absoluteFilePath());
        }
        else if (info.isFile())
            appendItem(manifest, path, destForRelative(relativeToRoot(path, manifest.rootSourceDir),
                manifest.rootSourceDir, DestLayout::NestUnderSourceFolder), QStringLiteral("VSTPlugin"));
    }
    return manifest;
}

ImportManifest ConfigDependencyScanner::scan(const QString& rootSource, const QString& configDir, DestLayout layout)
{
    Q_UNUSED(configDir);

    ImportManifest manifest;
    manifest.rootSource = QDir::cleanPath(QFileInfo(rootSource).absoluteFilePath());

    QFileInfo rootInfo(manifest.rootSource);
    if (!rootInfo.exists())
    {
        manifest.warnings.append(ConfigDependencyScanner::tr("Root file does not exist: %1").arg(manifest.rootSource));
        manifest.hasErrors = true;
        return manifest;
    }

    manifest.rootSourceDir = rootInfo.absoluteDir().absolutePath();
    manifest.rootDest = destForRelative(rootInfo.fileName(), manifest.rootSourceDir, layout);

    appendItem(manifest, manifest.rootSource, manifest.rootDest, QStringLiteral("Root"));

    QString suffix = rootInfo.suffix().toLower();
    if (suffix == QStringLiteral("txt"))
    {
        QSet<QString> visited;
        scanConfigFile(manifest, manifest.rootSource, manifest.rootSourceDir, layout, 0, visited);
    }

    return manifest;
}

}
