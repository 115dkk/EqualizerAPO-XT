/*
    This file is part of EqualizerAPO-XT.

    Walks an external EqualizerAPO config file (the one the user is
    about to import) and collects every file it references through
    Include, Convolution, and VSTPlugin commands. Recursion follows the
    same pattern as filters/IncludeFilterFactory.cpp so nested config
    trees are picked up.

    The scanner is read-only and side-effect free. ImportExecutor is the
    component that actually copies files.
*/

#pragma once

#include "ImportManifest.h"

#include <QString>

namespace EqAPO::Import
{

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
    static ImportManifest scan(const QString& rootSource, const QString& configDir);
};

}
