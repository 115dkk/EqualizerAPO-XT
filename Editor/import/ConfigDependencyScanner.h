/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    Builds the manifest for an external EqualizerAPO config import. It
    collects files referenced by Include, Convolution, MultiConvolution
    and SubwooferRouting Profile commands, and records VSTPlugin Library
    values as external references. Include files are scanned recursively.

    The scanner only reads the source tree. ImportExecutor copies the
    manifest's items.
*/

#pragma once

#include "ImportManifest.h"

#include <QString>

namespace EqAPO::Import
{

// Where the collected files land relative to the target config directory.
// NestUnderSourceFolder reproduces the card editors' behavior: everything
// goes under a subfolder named after the source's own folder, so an import
// never mixes into the config root. SourceFolderIsRoot maps the source
// folder 1:1 onto the target root (config.txt stays config.txt) - the
// layout the legacy-install migration needs, where the source folder IS
// the old config root.
enum class DestLayout
{
    NestUnderSourceFolder,
    SourceFolderIsRoot
};

class ConfigDependencyScanner
{
public:
    // Kept equal to the engine's file-local RECURSION_LIMIT.
    static constexpr int kRecursionLimit = 100;

    // Build a manifest rooted at rootSource. A .txt root is scanned for
    // dependencies; any other existing file produces one Root item. The
    // layout controls destination paths. configDir is retained for callers
    // but is not read or written.
    static ImportManifest scan(const QString& rootSource, const QString& configDir,
        DestLayout layout = DestLayout::NestUnderSourceFolder);
};

}
