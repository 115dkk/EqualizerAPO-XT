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
	// Velopack staging area so it is ready to apply on exit. Runs the network work on a
	// detached worker thread and returns immediately; repeat calls in the same session
	// are ignored. `repoUrl` is the full repository URL ("https://github.com/user/repo").
	// `channel` overrides the build channel; leave empty to use the installed channel.
	static void startBackgroundDownload(const std::string& repoUrl, const std::string& channel = std::string());

	// True once a downloaded update is staged and waiting to be applied.
	static bool hasPendingUpdate();

	// Apply the staged update silently without restarting, then exit the process. The
	// updater waits for this process to close before swapping files, so the new version
	// appears on the next launch. Does nothing (and returns) if no update is staged;
	// otherwise it does not return.
	static void applyPendingUpdateAndExit();
};
