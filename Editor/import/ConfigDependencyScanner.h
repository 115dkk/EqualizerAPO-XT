/*
	This file is part of EqualizerAPO-XT.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    Walks an external EqualizerAPO config file (the one the user is
    about to import) and collects every file it references through
    Include, Convolution, MultiConvolution, and VSTPlugin commands.
    Recursion follows the same pattern as filters/IncludeFilterFactory.cpp
    so nested config trees are picked up.

    The scanner is read-only and side-effect free. ImportExecutor is the
    component that actually copies files.
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
    // Maximum recursion depth, matches IncludeFilterFactory::RECURSION_LIMIT.
    static constexpr int kRecursionLimit = 100;

    // Build a manifest for importing rootSource into configDir.
    //
    // rootSource may be a config text file (.txt) — its references are
    // walked recursively — or a single binary (.wav, .dll, etc.) in
    // which case the manifest contains exactly one item. configDir is
    // only used to compute dest paths; the scanner never writes there.
    static ImportManifest scan(const QString& rootSource, const QString& configDir,
        DestLayout layout = DestLayout::NestUnderSourceFolder);
};

}
