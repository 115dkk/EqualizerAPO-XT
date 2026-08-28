/*
	This file is part of EqualizerAPO-XT.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ImportExecutor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

namespace EqAPO::Import
{

ExecutionResult ImportExecutor::execute(const ImportManifest& manifest, const QString& configDir)
{
    ExecutionResult result;

    if (configDir.isEmpty())
    {
        result.success = false;
        result.errors.append(QObject::tr("Import target directory is empty."));
        return result;
    }

    if (!QDir().mkpath(configDir))
    {
        result.success = false;
        result.errors.append(QObject::tr("Could not create config directory %1.").arg(configDir));
        return result;
    }

    for (const ImportItem& item : manifest.items)
    {
        if (!item.exists)
        {
            result.success = false;
            result.errors.append(QObject::tr("Source missing, skipped: %1").arg(item.sourceAbsolute));
            continue;
        }

        QString relNative = QDir::fromNativeSeparators(item.destRelative);
        QString destAbs = QDir(configDir).absoluteFilePath(relNative);
        QString destDir = QFileInfo(destAbs).absolutePath();
        if (!QDir().mkpath(destDir))
        {
            result.success = false;
            result.errors.append(QObject::tr("Could not create %1.").arg(destDir));
            continue;
        }

        if (QFile::exists(destAbs))
        {
            if (!QFile::remove(destAbs))
            {
                result.success = false;
                result.errors.append(QObject::tr("Could not overwrite %1.").arg(destAbs));
                continue;
            }
        }

        if (!QFile::copy(item.sourceAbsolute, destAbs))
        {
            result.success = false;
            result.errors.append(QObject::tr("Failed to copy %1 to %2.").arg(item.sourceAbsolute, destAbs));
            continue;
        }

        ++result.filesCopied;
        result.bytesCopied += item.sizeBytes;
    }

    return result;
}

}
