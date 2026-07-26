/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2025  EqualizerAPO-XT contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <string>

// The Editor's half of the update story: Velopack hooks plus in-app background
// download/apply through the Velopack SDK (which does its own feed parsing and
// package verification). The OTHER half is UpdateChecker.exe, a standalone
// notify-only tool that parses the GitHub/Velopack feeds itself in
// UpdateChecker/VelopackUpdateInfo.{h,cpp} (covered by EditorLogicTests). The
// two deliberately do not share feed code: UpdateChecker never downloads or
// applies, and this class never decides "is there a newer version" outside the
// SDK. Both read the channel baked in at build time via EAPO_UPDATE_CHANNEL.
class VelopackBootstrap
{
public:
	static bool isFirstRun();
	static bool isRestartingAfterUpdate();

	static bool isVelopackInstall();
	static std::wstring updateExePath();
	static std::wstring installRoot();
	static std::wstring currentBinDir();

	// Check the GitHub release feed and, if a newer build exists, download it into the
	// Velopack staging area so it is ready to apply on exit. Runs the network work on an
	// owned worker thread and returns immediately; repeat calls in the same session
	// are ignored. `repoUrl` is the full repository URL ("https://github.com/user/repo").
	// `channel` overrides the build channel; leave empty to use the installed channel.
	static void startBackgroundDownload(const std::string& repoUrl, const std::string& channel = std::string());

	// Wait for the owned update worker to finish. Call during orderly shutdown before
	// querying or applying a staged update.
	static void shutdown();

	// True once a downloaded update is staged and waiting to be applied.
	static bool hasPendingUpdate();

	// Version string of the staged update (e.g. L"2.12.0"); empty while no
	// update is staged. Feeds the Editor's in-app "update ready" notice.
	static std::wstring pendingUpdateVersion();

	// Apply the staged update silently without restarting, then exit the process. The
	// normal unelevated Editor first delegates to one elevated coordinator process.
	// Update.exe and both update hooks inherit that token, so the service stop and APO
	// re-registration share one UAC consent. Does nothing (and returns) if no update is
	// staged; otherwise it does not return.
	static void applyPendingUpdateAndExit();

	// Entry point for the short-lived elevated coordinator launched above. It reopens
	// the already-downloaded package through Velopack's public pending-update API,
	// starts Update.exe, then returns so the updater can proceed. The caller must exit
	// immediately with the returned status.
	static int runElevatedUpdateCoordinator(
		const std::string& repoUrl, const std::string& channel = std::string());
};
