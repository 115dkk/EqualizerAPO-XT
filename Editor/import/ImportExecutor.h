/*
	This file is part of EqualizerAPO-XT.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    Side-effecting half of the "import to config directory" flow.
    Takes an ImportManifest produced by ConfigDependencyScanner and
    copies every existing item into the config directory, creating
    sub-directories as needed and overwriting any existing destination.
*/

#pragma once

#include "ImportManifest.h"

#include <QString>
#include <QStringList>

#include <cstdint>

namespace EqAPO::Import
{

struct ExecutionResult
{
    bool success = true;
    int filesCopied = 0;
    qint64 bytesCopied = 0;
    QStringList errors;
};

class ImportExecutor
{
public:
    // Apply the manifest to configDir. Items whose source is missing are
    // skipped and reported as errors. Returns success only if every
    // existing item was copied without trouble.
    static ExecutionResult execute(const ImportManifest& manifest, const QString& configDir);
};

}
