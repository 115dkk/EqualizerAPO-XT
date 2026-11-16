/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ImportExecutor.h"

#include "platform/windows/Win32Resource.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace EqAPO::Import
{

namespace
{
QString cleanAbsolutePath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool sameCleanPath(const QString& left, const QString& right)
{
    return QString::compare(cleanAbsolutePath(left), cleanAbsolutePath(right), Qt::CaseInsensitive) == 0;
}

bool pathAtOrBelow(const QString& path, const QString& root)
{
    const QString cleanPath = cleanAbsolutePath(path);
    const QString cleanRoot = cleanAbsolutePath(root);
    if (QString::compare(cleanPath, cleanRoot, Qt::CaseInsensitive) == 0)
        return true;
    const QString prefix = cleanRoot.endsWith(QLatin1Char('/'))
        ? cleanRoot : cleanRoot + QLatin1Char('/');
    return cleanPath.startsWith(prefix, Qt::CaseInsensitive);
}

bool destinationForItem(const ImportItem& item, const QString& configRoot, QString& destination)
{
    const QString relative = QDir::fromNativeSeparators(item.destRelative);
    const QString cleanRelative = QDir::cleanPath(relative);
    if (relative.isEmpty() || cleanRelative == QStringLiteral(".")
        || QDir::isAbsolutePath(relative) || QDir::isAbsolutePath(cleanRelative))
        return false;

    destination = QDir::cleanPath(QDir(configRoot).absoluteFilePath(cleanRelative));
    return !sameCleanPath(destination, configRoot) && pathAtOrBelow(destination, configRoot);
}

bool sameFileIdentity(const QString& left, const QString& right)
{
    if (sameCleanPath(left, right))
        return true;

    const std::wstring leftNative = QDir::toNativeSeparators(left).toStdWString();
    const std::wstring rightNative = QDir::toNativeSeparators(right).toStdWString();
    winutil::UniqueHandle leftHandle(CreateFileW(leftNative.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    winutil::UniqueHandle rightHandle(CreateFileW(rightNative.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!leftHandle || !rightHandle)
        return false;

    BY_HANDLE_FILE_INFORMATION leftInfo = {};
    BY_HANDLE_FILE_INFORMATION rightInfo = {};
    if (!GetFileInformationByHandle(leftHandle.get(), &leftInfo)
        || !GetFileInformationByHandle(rightHandle.get(), &rightInfo))
        return false;
    return leftInfo.dwVolumeSerialNumber == rightInfo.dwVolumeSerialNumber
        && leftInfo.nFileIndexHigh == rightInfo.nFileIndexHigh
        && leftInfo.nFileIndexLow == rightInfo.nFileIndexLow;
}

bool isReparseDirectory(const QString& path)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes = GetFileAttributesW(native.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

void rememberSourceDirectories(const QString& source, const QString& sourceRoot,
    QSet<QString>& directories)
{
    if (sourceRoot.isEmpty())
        return;
    const QString cleanRoot = cleanAbsolutePath(sourceRoot);
    QString directory = cleanAbsolutePath(QFileInfo(source).absolutePath());
    while (pathAtOrBelow(directory, cleanRoot))
    {
        directories.insert(directory);
        if (sameCleanPath(directory, cleanRoot))
            break;
        QDir parent(directory);
        if (!parent.cdUp())
            break;
        const QString next = cleanAbsolutePath(parent.absolutePath());
        if (sameCleanPath(next, directory))
            break;
        directory = next;
    }
}

void pruneEmptySourceDirectories(const QSet<QString>& candidates)
{
    QStringList ordered = candidates.values();
    std::sort(ordered.begin(), ordered.end(), [](const QString& left, const QString& right) {
        return QDir::fromNativeSeparators(left).count(QLatin1Char('/'))
            > QDir::fromNativeSeparators(right).count(QLatin1Char('/'));
    });
    for (const QString& directory : ordered)
    {
        // Directory reparse points are never followed or removed while pruning
        // migration leftovers (audit #467 TD-01).
        if (!isReparseDirectory(directory))
            QDir().rmdir(directory);
    }
}
}

ExecutionResult ImportExecutor::execute(const ImportManifest& manifest, const QString& configDir,
    Disposition disposition)
{
    ExecutionResult result;
    QSet<QString> sourceDirectories;

    if (configDir.isEmpty())
    {
        result.success = false;
        result.errors.append(tr("Import target directory is empty."));
        return result;
    }

    const QString configRoot = cleanAbsolutePath(configDir);
    if (!QDir().mkpath(configRoot))
    {
        result.success = false;
        result.errors.append(tr("Could not create config directory %1.").arg(configDir));
        return result;
    }

    for (const ImportItem& item : manifest.items)
    {
        QString destAbs;
        if (!destinationForItem(item, configRoot, destAbs))
        {
            result.success = false;
            result.errors.append(tr("Destination outside the config directory, skipped: %1")
                .arg(item.destRelative));
            continue;
        }
        if (!item.exists)
        {
            result.success = false;
            result.errors.append(tr("Source missing, skipped: %1").arg(item.sourceAbsolute));
            continue;
        }

        const QString destDir = QFileInfo(destAbs).absolutePath();
        if (!QDir().mkpath(destDir))
        {
            result.success = false;
            result.errors.append(tr("Could not create %1.").arg(destDir));
            continue;
        }

        if (sameFileIdentity(item.sourceAbsolute, destAbs))
        {
            ++result.filesAlreadyInPlace;
            continue;
        }

        const QString tempAbs = destAbs + QStringLiteral(".eapo-import");
        // A hand-built manifest may name the reserved temporary path as its
        // source. Never remove it as if it were stale (audit #467 TD-01).
        if (QFile::exists(tempAbs) && sameFileIdentity(item.sourceAbsolute, tempAbs))
        {
            result.success = false;
            result.errors.append(tr("Failed to copy %1 to %2.").arg(item.sourceAbsolute, destAbs));
            continue;
        }
        if (QFile::exists(tempAbs) && !QFile::remove(tempAbs))
        {
            result.success = false;
            result.errors.append(tr("Could not overwrite %1.").arg(tempAbs));
            continue;
        }
        if (!QFile::copy(item.sourceAbsolute, tempAbs))
        {
            QFile::remove(tempAbs);
            result.success = false;
            result.errors.append(tr("Failed to copy %1 to %2.").arg(item.sourceAbsolute, destAbs));
            continue;
        }

        const std::wstring tempNative = QDir::toNativeSeparators(tempAbs).toStdWString();
        const std::wstring destNative = QDir::toNativeSeparators(destAbs).toStdWString();
        if (!MoveFileExW(tempNative.c_str(), destNative.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            QFile::remove(tempAbs);
            result.success = false;
            result.errors.append(tr("Failed to copy %1 to %2.").arg(item.sourceAbsolute, destAbs));
            continue;
        }

        ++result.filesCopied;
        const qint64 sourceSize = QFileInfo(item.sourceAbsolute).size();
        result.bytesCopied += sourceSize >= 0 ? sourceSize : item.sizeBytes;

        if (disposition != Disposition::RemoveSourcesAfterCopy)
            continue;
        const QFileInfo sourceInfo(item.sourceAbsolute);
        const QFileInfo destinationInfo(destAbs);
        if (!sourceInfo.exists() || !destinationInfo.exists()
            || sourceInfo.size() != destinationInfo.size())
        {
            result.warnings.append(tr("Source kept because the copied file size could not be verified: %1")
                .arg(item.sourceAbsolute));
            continue;
        }
        if (!QFile::remove(item.sourceAbsolute))
        {
            result.warnings.append(tr("Could not remove source after copying: %1")
                .arg(item.sourceAbsolute));
            continue;
        }
        ++result.filesRemovedFromSource;
        rememberSourceDirectories(item.sourceAbsolute, manifest.rootSourceDir, sourceDirectories);
    }

    if (disposition == Disposition::RemoveSourcesAfterCopy)
        pruneEmptySourceDirectories(sourceDirectories);
    return result;
}

}
