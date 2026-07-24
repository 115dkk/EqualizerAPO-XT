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

#include "LogHelper.h"
#include "OwnedBackgroundTask.h"

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

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
	// Probe the required size (includes the null terminator) so long values are
	// not silently dropped. Not-found and empty still yield an empty string.
	DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
	if (needed == 0)
		return std::wstring();
	std::wstring value(needed, L'\0');
	DWORD length = GetEnvironmentVariableW(name, value.data(), needed);
	if (length == 0 || length >= needed)
		return std::wstring();
	value.resize(length);
	return value;
}

// The session owns both the worker and every object that worker publishes. Keep
// the worker last so its destructor joins before the state above it is destroyed,
// even if a caller exits without the explicit orderly-shutdown hook.
struct UpdateSession
{
	std::mutex updateMutex;
	std::unique_ptr<Velopack::UpdateManager> manager;
	std::unique_ptr<Velopack::UpdateInfo> pendingUpdate;
	std::atomic<bool> updateReady{ false };
	OwnedBackgroundTask worker;
};

UpdateSession& updateSession()
{
	static UpdateSession session;
	return session;
}
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

	UpdateSession& session = updateSession();
	session.worker.startOnce([repoUrl, channel, &session]()
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

			std::lock_guard<std::mutex> lock(session.updateMutex);
			session.pendingUpdate = std::make_unique<Velopack::UpdateInfo>(*info);
			session.manager = std::move(manager);
			session.updateReady.store(true);
		}
		catch (const std::exception& e)
		{
			LogFStatic(L"[VelopackBootstrap] background update failed: %S", e.what());
			fprintf(stderr, "[VelopackBootstrap] background update failed: %s\n", e.what());
		}
		catch (...)
		{
			LogFStatic(L"[VelopackBootstrap] background update failed: unknown error");
			fprintf(stderr, "[VelopackBootstrap] background update failed: unknown error\n");
		}
	});
}

void VelopackBootstrap::shutdown()
{
	updateSession().worker.join();
}

bool VelopackBootstrap::hasPendingUpdate()
{
	return updateSession().updateReady.load();
}

std::wstring VelopackBootstrap::pendingUpdateVersion()
{
	UpdateSession& session = updateSession();
	std::lock_guard<std::mutex> lock(session.updateMutex);
	if (!session.updateReady.load() || !session.pendingUpdate)
		return std::wstring();
	// Velopack version strings are plain ASCII semver, so the widening is a
	// straight code-unit copy.
	const std::string& version = session.pendingUpdate->TargetFullRelease.Version;
	return std::wstring(version.begin(), version.end());
}

void VelopackBootstrap::applyPendingUpdateAndExit()
{
	UpdateSession& session = updateSession();
	std::lock_guard<std::mutex> lock(session.updateMutex);
	if (!session.updateReady.load() || !session.manager || !session.pendingUpdate)
		return;

	try
	{
		// Apply silently and do not restart: the user closed the app, so we just swap
		// files in the background and let the new version come up on the next launch.
		session.manager->WaitExitThenApplyUpdates(*session.pendingUpdate, /*silent*/ true, /*restart*/ false);
	}
	catch (const std::exception& e)
	{
		LogFStatic(L"[VelopackBootstrap] apply update failed: %S", e.what());
		fprintf(stderr, "[VelopackBootstrap] apply update failed: %s\n", e.what());
		return;
	}

	// The updater is now waiting for this process to exit before swapping files.
	std::exit(0);
}
