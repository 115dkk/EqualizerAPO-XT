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
#include <optional>
#include <vector>

class QWidget;
class IRegistry;

namespace EqAPO::Import
{
namespace CallerProfileCheck { class FileSystem; }

class LegacyMigration
{
public:
    // %LOCALAPPDATA%\EqualizerAPO-XT\config for the current user; empty when
    // the environment variable is missing.
    // %LOCALAPPDATA%\EqualizerAPO-XT\config, read as UTF-16 (audit #348
    // TD-05: the ANSI qgetenv turned characters outside the code page into
    // '?', and the elevated hook wrote that path to HKLM ConfigPath).
    static QString stableConfigRoot();
    // The folder the Editor edits: HKLM ConfigPath when it can be read, else
    // the stable root when it exists, else the working directory. One rule
    // for the startup path and the file dialog's sidebar (audit #348 TD-50);
    // a registry error is logged and falls through instead of escaping.
    static QString configRoot(const IRegistry& registry);

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
    struct Handoff
    {
        std::optional<std::wstring> localAppData;
        std::wstring outcome;
        std::wstring migratedFrom;
        std::wstring migratedFiles;
    };
    // File work is forbidden when elevated. The returned action is a hint,
    // not authority: recordPreparedHookStep reclassifies the current HKLM value.
    static std::wstring prepareHookStep(const std::wstring& exeDir, const IRegistry& registry,
        Handoff* details = nullptr);
    static std::wstring prepareHookStep(const std::wstring& exeDir, Handoff* details = nullptr);
    static Handoff parseHandoff(const std::vector<std::wstring>& arguments);
    static void runElevatedHookStep(const std::wstring& exeDir, const Handoff& handoff);
    static bool recordPreparedHookStep(const Handoff& handoff, IRegistry& registry,
        const CallerProfileCheck::FileSystem* fileSystem = nullptr);

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
