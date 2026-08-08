/*
    This file is part of EqualizerAPO-XT.
*/

#include "ConfigDependencyScanner.h"
#include "../ConfigFileCodec.h"
#include "../widgets/FilterCardModel.h"
#include "filters/ConvolutionFilePath.h"
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

// The engine unquotes convolution paths in ConvolutionFilePath::resolve, so a
// quoted reference is a valid line; the scanner must look at the same file.
QString stripSurroundingQuotes(const QString& text)
{
    QString trimmed = text.trimmed();
    if (trimmed.length() >= 2 && trimmed.startsWith(QLatin1Char('"')) && trimmed.endsWith(QLatin1Char('"')))
        return trimmed.mid(1, trimmed.length() - 2);
    return trimmed;
}

// The path portion of the line for the given reference command. The
// convolution family and VSTPlugin share their engine grammar so routing
// factors, mappings, state and parameter pairs never leak into the path.
QString referencePath(const QString& keyword, const QString& parameters)
{
    if (keyword == QStringLiteral("MultiConvolution"))
    {
        MultiConvolutionCommand command;
        if (!MultiConvolutionCommand::parse(L"MultiConvolution", parameters.toStdWString(), command))
            return QString();
        return stripSurroundingQuotes(QString::fromStdWString(command.path));
    }
    if (keyword == QStringLiteral("Convolution"))
        return stripSurroundingQuotes(parameters);
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
        return stripSurroundingQuotes(QString::fromStdWString(command.payload));
    }
    return parameters;
}

QString resolveAbsolute(const QString& reference, const QString& configPath)
{
    const std::wstring resolved = ConvolutionFilePath::resolve(
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
    if (rel.isEmpty() || rel.startsWith(QStringLiteral("..")))
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
        manifest.warnings.append(QObject::tr("Missing file: %1").arg(sourceAbs));
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
        manifest.warnings.append(QObject::tr("Recursion limit reached at %1; nested references were not followed.").arg(sourceTxtAbs));
        manifest.hasErrors = true;
        return;
    }

    if (visited.contains(sourceTxtAbs))
        return;
    visited.insert(sourceTxtAbs);

    const ConfigFileCodec::ReadResult readResult = ConfigFileCodec::readConfig(sourceTxtAbs);
    if (!readResult.ok)
    {
        manifest.warnings.append(QObject::tr("Cannot open %1 for scanning.").arg(sourceTxtAbs));
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
                manifest.warnings.append(QObject::tr(
                    "VSTPlugin line has no Library reference: %1 (in %2)")
                    .arg(parameters, sourceTxtAbs));
                manifest.hasErrors = true;
            }
            else if (!manifest.externalReferences.contains(reference))
                manifest.externalReferences.append(reference);

            // Plugin binaries are machine-installed dependencies. Preserve the
            // config line verbatim, but never copy the binary into config.
            continue;
        }

        QString refAbs = resolveAbsolute(reference, sourceTxtAbs);
        if (refAbs.isEmpty())
            continue;

        QString rel = relativeToRoot(refAbs, rootSourceDir);
        if (rel.isEmpty())
        {
            manifest.warnings.append(QObject::tr("Reference outside the source folder will be skipped: %1 (in %2)")
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

ImportManifest ConfigDependencyScanner::scan(const QString& rootSource, const QString& configDir, DestLayout layout)
{
    Q_UNUSED(configDir);

    ImportManifest manifest;
    manifest.rootSource = QDir::cleanPath(QFileInfo(rootSource).absoluteFilePath());

    QFileInfo rootInfo(manifest.rootSource);
    if (!rootInfo.exists())
    {
        manifest.warnings.append(QObject::tr("Root file does not exist: %1").arg(manifest.rootSource));
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
