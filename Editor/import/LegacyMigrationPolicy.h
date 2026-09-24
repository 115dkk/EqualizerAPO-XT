/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    Pure decision logic for the legacy-install config migration: given the
    ConfigPath value the audio pipeline currently trusts, decide whether the
    installer hook should adopt it, migrate it, or leave it alone. No
    registry or filesystem access lives here (EditorLogicTests covers the
    decisions); LegacyMigration is the side-effecting orchestrator.
*/

#pragma once

#include <QString>

namespace EqAPO::Import
{

class LegacyMigrationPolicy
{
public:
    enum class Action
    {
        // No ConfigPath (or an empty one): claim the stable root outright.
        AdoptStableRoot,
        // ConfigPath already points at the stable root; nothing to change.
        AlreadyOurs,
        // ConfigPath points into a legacy Equalizer APO install: import the
        // referenced config tree, then repoint.
        MigrateLegacy,
        // ConfigPath points into an EqualizerAPO-XT ...\current\config dir,
        // which Velopack recreates on every update: rescue the whole folder,
        // then repoint.
        MigrateVolatileXt,
        // ConfigPath points somewhere the user chose on purpose: respect it.
        RespectCustom
    };

    // The canonical XT config root: %LOCALAPPDATA%\EqualizerAPO-XT\config.
    // Lives outside every Velopack-managed folder (variant install roots and
    // their current\ dirs), so it survives updates, SIMD-variant switches,
    // and reinstalls. localAppData is passed in so the derivation is testable.
    static QString stableConfigRoot(const QString& localAppData);

    // True for paths of the shape ...\EqualizerAPO-XT-<variant>\current\config:
    // the packaged config dir inside a Velopack current folder. Velopack
    // recreates current\ wholesale on every update (observed: all file
    // creation times equal the update time), so user configs here are one
    // update away from deletion.
    static bool isVolatileXtConfigDir(const QString& dir);

    // True when the config dir sits under a folder carrying the original
    // Equalizer APO installer's name ("EqualizerAPO" or "Equalizer APO").
    // Complements the on-disk binary markers: uninstalling the legacy APO
    // removes EqualizerAPO.dll and Uninstall.exe but leaves the config
    // folder (and the stale ConfigPath) behind — the exact shape of the
    // field reports this migration exists for.
    static bool hasLegacyApoFolderName(const QString& dir);

    // The classification. legacyMarkersPresent is the caller's on-disk
    // verdict for existingConfigPath's parent (EqualizerAPO.dll or the NSIS
    // Uninstall.exe next to the config dir).
    static Action classify(const QString& existingConfigPath, const QString& stableRoot,
        bool legacyMarkersPresent, bool volatileXt);

    // If path lies under fromRoot, the same relative location under toRoot
    // (forward slashes); otherwise an empty string. Case-insensitive,
    // separator-insensitive, and boundary-safe ("...\configX" is not under
    // "...\config"). Pure string logic for remapping saved open-file and
    // recent-file paths after a migration.
    static QString remapUnderRoot(const QString& path, const QString& fromRoot, const QString& toRoot);
};

}
