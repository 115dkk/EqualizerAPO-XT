/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    Side-effecting orchestrator of the legacy-install config migration. The
    elevated Velopack install/update hook calls runElevatedHookStep() after
    APO registration: it decides via LegacyMigrationPolicy what the trusted
    ConfigPath should become, imports the legacy config tree (Include chains,
    convolution IRs) through the existing import module when one is found,
    repoints HKLM ConfigPath at the stable XT config root, and leaves
    breadcrumbs the Editor turns into a one-time startup notice.
*/

#pragma once

#include <QString>

#include <string>

class QWidget;
class IRegistry;

namespace EqAPO::Import
{

class LegacyMigration
{
public:
    // %LOCALAPPDATA%\EqualizerAPO-XT\config for the current user; empty when
    // the environment variable is missing.
    static QString stableConfigRoot();

    // On-disk verdict for a candidate legacy config dir: its parent holds an
    // Equalizer APO install (EqualizerAPO.dll or the NSIS Uninstall.exe).
    static bool looksLikeLegacyApoConfigDir(const QString& configDir);

    // The whole hook-side step. Runs elevated (registry writes go to HKLM);
    // must not show UI. exeDir is the install's current\ dir, whose config\
    // subfolder carries the shipped sample configs. The registry-taking
    // overload is the real implementation (audit #275 C1): the machine
    // -changing writes here (ConfigPath, the migration breadcrumbs) used to
    // bypass the port and were untestable off a real machine; EditorLogicTests
    // now drives this through a fake registry.
    static void runElevatedHookStep(const std::wstring& exeDir);
    static void runElevatedHookStep(const std::wstring& exeDir, IRegistry& registry);

    // "--migration-dry-run": print the classification and the manifest the
    // hook would act on, write nothing. Field diagnostics for "why did my
    // config not move" reports. Returns the process exit code.
    static int dryRun();

    // Editor startup: if the hook migrated a config tree this user has not
    // been told about yet, show a one-time notice with the old and new roots.
    static void maybeShowStartupNotice(QWidget* parent);

    // Saved open-file and recent-file paths keep pointing into the migrated
    // legacy folder, which the audio pipeline no longer reads — editing a
    // restored tab there would silently change nothing. If path lies under
    // the migrated root, answer its stable-root copy (copying the file over
    // on demand when the migration's referenced-set import did not carry it)
    // and port the per-file Editor preferences to the new key. Any other
    // path comes back unchanged.
    static QString adoptMigratedFile(const QString& path);
};

}
