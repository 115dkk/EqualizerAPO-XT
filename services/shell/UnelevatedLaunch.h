/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <string>
#include "platform/windows/Win32Resource.h"
#include "services/logging/TaggedLogger.h"

namespace winutil
{
template<class ElevationQuery, class ShellLauncher, class PlainLauncher>
auto chooseUnelevatedLaunch(const ElevationQuery& isElevated, const ShellLauncher& shell,
	const PlainLauncher& plain)
{
	return isElevated() ? shell() : plain();
}

struct UnelevatedLaunchResult
{
	DWORD error = ERROR_SUCCESS;
	DWORD processId = 0;
};

// commandLine is writable for both CreateProcess APIs.
// cppcheck-suppress passedByValue
inline UnelevatedLaunchResult launchUnelevated(const std::wstring& executable, std::wstring commandLine,
	DWORD flags = 0)
{
	constexpr logging::TaggedLogger logLine(L"UnelevatedLaunch");
	UniqueHandle callerToken;
	TOKEN_ELEVATION elevation{};
	DWORD returned = 0;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, callerToken.put())
		|| !GetTokenInformation(callerToken.get(), TokenElevation, &elevation, sizeof(elevation), &returned))
		return {GetLastError(), 0};

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	const auto directory = executable.substr(0, executable.find_last_of(L"\\/"));
	const auto plain = [&]() -> DWORD {
		return CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
			flags, nullptr, directory.c_str(), &startup, &process) ? ERROR_SUCCESS : GetLastError();
	};
	const auto shell = [&]() -> DWORD {
		const HWND window = GetShellWindow();
		if (!window)
		{
			// No interactive user is safer than loading a user-writable plugin
			// as administrator (testHelperLaunchDropsTheAdministratorToken,
			// audit #467 TD-15). Never fall back to the caller's token.
			logLine(L"WARN", L"No interactive shell; helper not started");
			return ERROR_NOT_FOUND;
		}
		DWORD pid = 0;
		if (!GetWindowThreadProcessId(window, &pid))
			return GetLastError();
		UniqueHandle shellProcess(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
		if (!shellProcess)
			return GetLastError();
		UniqueHandle token;
		if (!OpenProcessToken(shellProcess.get(), TOKEN_QUERY | TOKEN_DUPLICATE, token.put()))
			return GetLastError();
		TOKEN_ELEVATION shellElevation{};
		if (!GetTokenInformation(token.get(), TokenElevation, &shellElevation, sizeof(shellElevation), &returned))
			return GetLastError();
		if (shellElevation.TokenIsElevated)
			return ERROR_ACCESS_DENIED;
		UniqueHandle primary;
		if (!DuplicateTokenEx(token.get(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY,
			nullptr, SecurityImpersonation, TokenPrimary, primary.put()))
			return GetLastError();
		// A null environment uses the shell user's profile, not an over-the-shoulder
		// administrator's LOCALAPPDATA. The shell already has that profile loaded.
		return CreateProcessWithTokenW(primary.get(), 0, executable.c_str(), commandLine.data(),
			flags, nullptr, directory.c_str(), &startup, &process) ? ERROR_SUCCESS : GetLastError();
	};
	const DWORD error = chooseUnelevatedLaunch([&]() { return elevation.TokenIsElevated != 0; }, shell, plain);
	if (error != ERROR_SUCCESS)
	{
		logLine(L"WARN", L"Helper not started (error=%lu): %s", error, executable.c_str());
		return {error, 0};
	}
	UniqueHandle thread(process.hThread);
	UniqueHandle child(process.hProcess);
	return {ERROR_SUCCESS, process.dwProcessId};
}
}
