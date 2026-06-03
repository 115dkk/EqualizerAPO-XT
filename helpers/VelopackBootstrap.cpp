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

#include "VelopackBootstrap.h"

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Velopack.hpp pulls in the C ABI header; include it after windows.h so the
// platform headers resolve in the expected order.
#include <Velopack.hpp>

namespace
{
std::wstring exeDirectory()
{
	wchar_t buffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	if (length == 0)
		return std::wstring();
	std::wstring path(buffer, length);
	size_t slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return std::wstring();
	return path.substr(0, slash);
}

bool fileExists(const std::wstring& path)
{
	DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring envVar(const wchar_t* name)
{
	wchar_t buffer[256];
	DWORD length = GetEnvironmentVariableW(name, buffer, 256);
	if (length == 0 || length >= 256)
		return std::wstring();
	return std::wstring(buffer, length);
}

// State for the staged update produced by the background worker. The UpdateManager
// must outlive the worker thread so applyPendingUpdateAndExit() can drive the apply,
// hence it is kept here rather than on the worker's stack.
std::mutex g_updateMutex;
std::unique_ptr<Velopack::UpdateManager> g_manager;
std::unique_ptr<Velopack::UpdateInfo> g_pendingUpdate;
std::atomic<bool> g_downloadStarted{ false };
std::atomic<bool> g_updateReady{ false };
}

bool VelopackBootstrap::isFirstRun()
{
	return !envVar(L"VELOPACK_FIRSTRUN").empty();
}

bool VelopackBootstrap::isRestartingAfterUpdate()
{
	return !envVar(L"VELOPACK_RESTART").empty();
}

std::wstring VelopackBootstrap::currentBinDir()
{
	return exeDirectory();
}

std::wstring VelopackBootstrap::installRoot()
{
	std::wstring bin = exeDirectory();
	if (bin.empty())
		return std::wstring();
	size_t slash = bin.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return bin;
	std::wstring leaf = bin.substr(slash + 1);
	if (_wcsicmp(leaf.c_str(), L"current") != 0)
		return bin;
	return bin.substr(0, slash);
}

std::wstring VelopackBootstrap::updateExePath()
{
	std::wstring root = installRoot();
	if (root.empty())
		return std::wstring();
	std::wstring candidate = root + L"\\Update.exe";
	if (fileExists(candidate))
		return candidate;
	return std::wstring();
}

bool VelopackBootstrap::isVelopackInstall()
{
	return !updateExePath().empty();
}

void VelopackBootstrap::startBackgroundDownload(const std::string& repoUrl, const std::string& channel)
{
	if (!isVelopackInstall())
		return;
	if (repoUrl.empty())
		return;

	// Run the check + download at most once per session.
	bool expected = false;
	if (!g_downloadStarted.compare_exchange_strong(expected, true))
		return;

	std::thread([repoUrl, channel]()
	{
		try
		{
			Velopack::UpdateOptions options{};
			options.AllowVersionDowngrade = false;
			options.MaximumDeltasBeforeFallback = 10;
			if (!channel.empty())
				options.ExplicitChannel = channel;

			auto manager = std::make_unique<Velopack::UpdateManager>(
				std::make_unique<Velopack::GithubSource>(repoUrl, "", false),
				&options);

			std::optional<Velopack::UpdateInfo> info = manager->CheckForUpdates();
			if (!info.has_value())
				return; // already up to date

			manager->DownloadUpdates(*info);

			std::lock_guard<std::mutex> lock(g_updateMutex);
			g_pendingUpdate = std::make_unique<Velopack::UpdateInfo>(*info);
			g_manager = std::move(manager);
			g_updateReady.store(true);
		}
		catch (const std::exception& e)
		{
			fprintf(stderr, "[VelopackBootstrap] background update failed: %s\n", e.what());
		}
		catch (...)
		{
			fprintf(stderr, "[VelopackBootstrap] background update failed: unknown error\n");
		}
	}).detach();
}

bool VelopackBootstrap::hasPendingUpdate()
{
	return g_updateReady.load();
}

void VelopackBootstrap::applyPendingUpdateAndExit()
{
	std::lock_guard<std::mutex> lock(g_updateMutex);
	if (!g_updateReady.load() || !g_manager || !g_pendingUpdate)
		return;

	try
	{
		// Apply silently and do not restart: the user closed the app, so we just swap
		// files in the background and let the new version come up on the next launch.
		g_manager->WaitExitThenApplyUpdates(*g_pendingUpdate, /*silent*/ true, /*restart*/ false);
	}
	catch (const std::exception& e)
	{
		fprintf(stderr, "[VelopackBootstrap] apply update failed: %s\n", e.what());
		return;
	}

	// The updater is now waiting for this process to exit before swapping files.
	std::exit(0);
}
