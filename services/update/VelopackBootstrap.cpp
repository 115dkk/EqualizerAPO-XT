/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2025  EqualizerAPO-XT contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "VelopackBootstrap.h"

#include "UpdateSession.h"

#include "services/security/AudioEngineAccess.h"
#include "services/logging/LogHelper.h"
#include "platform/windows/WindowsPath.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

// Velopack.hpp pulls in the C ABI header; include it after windows.h so the
// platform headers resolve in the expected order.
#include <Velopack.hpp>

namespace
{
using pathutil::exeDirectory;
using pathutil::fileExists;

Velopack::UpdateOptions updateOptions(const std::string& channel)
{
	Velopack::UpdateOptions options{};
	options.AllowVersionDowngrade = false;
	options.MaximumDeltasBeforeFallback = 10;
	if (!channel.empty())
		options.ExplicitChannel = channel;
	return options;
}

std::wstring envVar(const wchar_t* name)
{
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

class VelopackUpdateClient final : public IUpdateClient
{
public:
	VelopackUpdateClient(std::string repoUrl, std::string channel)
		: repoUrl(std::move(repoUrl)), channel(std::move(channel))
	{
	}

	std::optional<std::wstring> stageAvailableUpdate() override
	{
		ensureManager();
		std::optional<Velopack::UpdateInfo> info = manager->CheckForUpdates();
		if (!info.has_value())
			return std::nullopt;

		manager->DownloadUpdates(*info);
		pendingUpdate = std::make_unique<Velopack::UpdateInfo>(*info);
		const std::string& version = pendingUpdate->TargetFullRelease.Version;
		return std::wstring(version.begin(), version.end());
	}

	void applyStagedUpdate() override
	{
		if (!manager || !pendingUpdate)
			throw std::logic_error("no staged Velopack update");

		manager->WaitExitThenApplyUpdates(
			*pendingUpdate,
			/*silent*/ true,
			/*restart*/ false);
	}

	bool applyPendingRestartUpdate() override
	{
		ensureManager();
		std::optional<Velopack::VelopackAsset> pendingRestart = manager->UpdatePendingRestart();
		if (!pendingRestart.has_value())
			return false;

		manager->WaitExitThenApplyUpdates(
			*pendingRestart,
			/*silent*/ true,
			/*restart*/ false);
		return true;
	}

private:
	void ensureManager()
	{
		if (manager)
			return;

		Velopack::UpdateOptions options = updateOptions(channel);
		manager = std::make_unique<Velopack::UpdateManager>(
			std::make_unique<Velopack::GithubSource>(repoUrl, "", false),
			&options);
	}

	std::string repoUrl;
	std::string channel;
	std::unique_ptr<Velopack::UpdateManager> manager;
	std::unique_ptr<Velopack::UpdateInfo> pendingUpdate;
};
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

std::unique_ptr<UpdateSession> VelopackBootstrap::createUpdateSession(
	const std::string& repoUrl,
	const std::string& channel)
{
	if (!isVelopackInstall() || repoUrl.empty())
		return nullptr;

	return std::make_unique<UpdateSession>(
		std::make_unique<VelopackUpdateClient>(repoUrl, channel),
		[](const std::string& error) {
			LogFStatic(L"[VelopackBootstrap] update operation failed: %S", error.c_str());
		});
}

bool VelopackBootstrap::launchElevatedUpdateCoordinator()
{
	wchar_t exePath[MAX_PATH];
	const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	if (length == 0 || length == MAX_PATH)
	{
		LogFStatic(
			L"[VelopackBootstrap] failed to resolve Editor path for update elevation (gle=%lu)",
			GetLastError());
		return false;
	}

	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOASYNC;
	info.lpVerb = L"runas";
	info.lpFile = exePath;
	info.lpParameters = L"--eapo-apply-update-elevated";
	info.nShow = SW_HIDE;

	if (ShellExecuteExW(&info))
		return true;

	LogFStatic(
		L"[VelopackBootstrap] update elevation was not started (gle=%lu)",
		GetLastError());
	return false;
}

int VelopackBootstrap::runElevatedUpdateCoordinator(
	const std::string& repoUrl,
	const std::string& channel)
{
	if (!AudioEngineAccess::isElevated())
	{
		LogFStatic(L"[VelopackBootstrap] refusing to coordinate an update without elevation");
		return 1;
	}
	if (!isVelopackInstall() || repoUrl.empty())
		return 1;

	VelopackUpdateClient client(repoUrl, channel);
	const UpdateApplyOutcome outcome = coordinatePendingRestartUpdate(client);
	if (outcome == UpdateApplyOutcome::UpdaterLaunched)
		return 0;
	if (outcome == UpdateApplyOutcome::NoUpdate)
		LogFStatic(L"[VelopackBootstrap] elevated coordinator found no staged update");
	else
		LogFStatic(L"[VelopackBootstrap] elevated update coordination failed");
	return 1;
}
