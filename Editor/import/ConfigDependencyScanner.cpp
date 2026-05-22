/*
    This file is part of EqualizerAPO-XT.
*/

#include "ConfigDependencyScanner.h"
#include "../widgets/FilterCardModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

namespace EqAPO::Import
{

namespace
{

bool isReferenceCommand(const QString& commandLower)
{
    return commandLower == QStringLiteral("include")
        || commandLower == QStringLiteral("convolution")
        || commandLower == QStringLiteral("vstplugin");
}

QString kindForCommand(const QString& commandLower)
{
    if (commandLower == QStringLiteral("include")) return QStringLiteral("Include");
    if (commandLower == QStringLiteral("convolution")) return QStringLiteral("Convolution");
    if (commandLower == QStringLiteral("vstplugin")) return QStringLiteral("VSTPlugin");
    return commandLower;
}

QString resolveAbsolute(const QString& reference, const QString& baseDir)
{
    QString trimmed = reference.trimmed();
    if (trimmed.isEmpty())
        return QString();

    QFileInfo info(trimmed);
    if (info.isAbsolute())
        return QDir::cleanPath(info.absoluteFilePath());

    return QDir::cleanPath(QDir(baseDir).absoluteFilePath(trimmed));
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

    QFile file(sourceTxtAbs);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        manifest.warnings.append(QObject::tr("Cannot open %1 for scanning.").arg(sourceTxtAbs));
        manifest.hasErrors = true;
        return;
    }

    QFileInfo configInfo(sourceTxtAbs);
    QString baseDir = configInfo.absoluteDir().absolutePath();

    QTextStream stream(&file);
    while (!stream.atEnd())
    {
        QString line = stream.readLine();
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;

        QString parameters;
        QString command = FilterCardModel::commandForLine(line, &parameters);
        QString commandLower = command.toLower();

        if (!isReferenceCommand(commandLower))
            continue;

        QString refAbs = resolveAbsolute(parameters, baseDir);
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

        QString rootName = QFileInfo(rootSourceDir).fileName();
        QString destRel = rootName.isEmpty() ? rel : rootName + QStringLiteral("/") + rel;

        appendItem(manifest, refAbs, destRel, kindForCommand(commandLower));

        if (commandLower == QStringLiteral("include"))
            scanConfigFile(manifest, refAbs, rootSourceDir, depth + 1, visited);
    }
}

}

ImportManifest ConfigDependencyScanner::scan(const QString& rootSource, const QString& configDir)
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
    QString rootName = QFileInfo(manifest.rootSourceDir).fileName();
    QString rootRel = rootName.isEmpty()
        ? rootInfo.fileName()
        : rootName + QStringLiteral("/") + rootInfo.fileName();
    manifest.rootDest = rootRel;

    appendItem(manifest, manifest.rootSource, manifest.rootDest, QStringLiteral("Root"));

    QString suffix = rootInfo.suffix().toLower();
    if (suffix == QStringLiteral("txt"))
    {
        QSet<QString> visited;
        scanConfigFile(manifest, manifest.rootSource, manifest.rootSourceDir, 0, visited);
    }

    return manifest;
}

}
