/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    Builds the manifest for an external EqualizerAPO config import. It
    collects files referenced by Include, Convolution, MultiConvolution
    and SubwooferRouting Profile commands, and records VSTPlugin Library
    values as external references. Include files are scanned recursively.
    The VST card's explicit library import instead collects a single binary
    or every plain file in a bundle identified by the engine.

    The scanner only reads the source tree. ImportExecutor copies the
    manifest's items.
*/

#pragma once

#include "ImportManifest.h"

#include <QCoreApplication>
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
    Q_DECLARE_TR_FUNCTIONS(ConfigDependencyScanner)

public:
    // Kept equal to the engine's file-local RECURSION_LIMIT.
    static constexpr int kRecursionLimit = 100;

    // Build a manifest rooted at rootSource. A .txt root is scanned for
    // dependencies; any other existing file produces one Root item. The
    // layout controls destination paths. configDir is retained for callers
    // but is not read or written.
    static ImportManifest scan(const QString& rootSource, const QString& configDir,
        DestLayout layout = DestLayout::NestUnderSourceFolder);

    // The manifest for importing the library a VSTPlugin line names (the VST
    // card's Import button). A single-file plug-in gives the manifest scan()
    // gives for it. A VST3 bundle, named either by its folder or by the module
    // inside it, is imported as a unit: one item per plain file below the bundle
    // folder, so the copy keeps Contents, the platform folder and Resources.
    // rootDest is the destination of what the line names (the folder or the
    // module), so the rewritten Library value points at the same thing.
    static ImportManifest scanPluginLibrary(const QString& library, const QString& configPath);
};

}
