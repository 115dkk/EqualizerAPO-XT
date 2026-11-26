/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    Side-effecting half of the "import to config directory" flow.
    Takes an ImportManifest produced by ConfigDependencyScanner and
    copies every existing item into the config directory, creating
    sub-directories as needed and overwriting any existing destination.
*/

#pragma once

#include "ImportManifest.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace EqAPO::Import
{

enum class Disposition
{
    KeepSources,
    RemoveSourcesAfterCopy
};

struct ExecutionResult
{
    bool success = true;
    int filesCopied = 0;
    int filesAlreadyInPlace = 0;
    // Absolute destinations that already name the source file, for per-file grants.
    QStringList alreadyInPlace;
    int filesRemovedFromSource = 0;
    qint64 bytesCopied = 0;
    QStringList warnings;
    QStringList errors;
};

class ImportExecutor
{
    Q_DECLARE_TR_FUNCTIONS(ImportExecutor)

public:
    // Apply the manifest to configDir. Items whose source is missing are
    // skipped and reported as errors. Sources are kept unless the caller
    // explicitly requests migration move semantics; even then, a source is
    // removed only after its replacement has been committed and verified.
    static ExecutionResult execute(const ImportManifest& manifest, const QString& configDir,
        Disposition disposition = Disposition::KeepSources);
};

}
