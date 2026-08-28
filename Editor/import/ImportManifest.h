/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    Data structures describing what an "Import to config directory" pass
    will touch. Built by ConfigDependencyScanner, consumed by the GUI
    dialog, and finally executed by ImportExecutor.
*/

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

namespace EqAPO::Import
{

struct ImportItem
{
    // Absolute path of the file on the user's machine.
    QString sourceAbsolute;
    // Path relative to the chosen config directory where it should land
    // (forward slashes; ImportExecutor turns this into native separators).
    QString destRelative;
    // Size in bytes if the file exists, 0 otherwise.
    qint64 sizeBytes = 0;
    // Was the file present on disk when the scan ran?
    bool exists = true;
    // What kind of reference triggered this item (Include, Convolution,
    // VSTPlugin, Root). Purely informational; useful for the dialog.
    QString kind;
};

struct ImportManifest
{
    // Absolute path of the root config file the user picked.
    QString rootSource;
    // Absolute path of the directory that becomes the import root - every
    // dependency relative path is resolved against this. Usually
    // QFileInfo(rootSource).absoluteDir().
    QString rootSourceDir;
    // dest path of the root config file relative to the config directory.
    QString rootDest;
    // Every file we plan to copy (root + dependencies). Items keep a
    // stable order so the dialog can display them in scan order.
    QVector<ImportItem> items;
    // VST binaries stay at their configured locations. Their Library values
    // are recorded here for diagnostics but are deliberately not copy items.
    QStringList externalReferences;
    // Non-fatal messages (file not found, dependency outside root, etc).
    QStringList warnings;
    // Sum of sizeBytes across all existing items, for the confirmation UI.
    qint64 totalBytes = 0;
    // True if any item could not be located or recursion blew the limit.
    bool hasErrors = false;
};

}
