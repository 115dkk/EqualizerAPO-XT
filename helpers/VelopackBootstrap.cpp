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
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

bool VelopackBootstrap::triggerBackgroundUpdate(const std::wstring& githubRepo)
{
	std::wstring updateExe = updateExePath();
	if (updateExe.empty())
		return false;
	if (githubRepo.empty())
		return false;

	std::wstring repo = githubRepo;
	if (repo.find(L"://") == std::wstring::npos)
		repo = L"https://github.com/" + repo;

	std::wstring commandLine = L"\"" + updateExe + L"\" --silent --update " + repo;
	std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back(L'\0');

	STARTUPINFOW startupInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION processInfo;
	ZeroMemory(&processInfo, sizeof(processInfo));

	if (!CreateProcessW(updateExe.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &startupInfo, &processInfo))
	{
		fwprintf(stderr, L"[VelopackBootstrap] CreateProcess for %s failed: %lu\n",
			updateExe.c_str(), GetLastError());
		return false;
	}

	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);
	return true;
}
