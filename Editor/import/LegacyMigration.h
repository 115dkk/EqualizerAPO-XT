/*
    This file is part of EqualizerAPO-XT.

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
    // subfolder carries the shipped sample configs.
    static void runElevatedHookStep(const std::wstring& exeDir);

    // "--migration-dry-run": print the classification and the manifest the
    // hook would act on, write nothing. Field diagnostics for "why did my
    // config not move" reports. Returns the process exit code.
    static int dryRun();

    // Editor startup: if the hook migrated a config tree this user has not
    // been told about yet, show a one-time notice with the old and new roots.
    static void maybeShowStartupNotice(QWidget* parent);
};

}
