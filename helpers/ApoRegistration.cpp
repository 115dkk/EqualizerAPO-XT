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

#include "ApoRegistration.h"

#include <cstdio>
#include <memory>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include "AbstractAPOInfo.h"
#include "DeviceAPOInfo.h"
#include "RegistryHelper.h"
#include "ServiceHelper.h"

namespace
{
constexpr wchar_t kRegPath[] = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO";
constexpr wchar_t kAudioRegPath[] = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio";
constexpr wchar_t kAudioServiceName[] = L"AudioSrv";

std::wstring systemPath()
{
	wchar_t buffer[MAX_PATH];
	UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
	if (length == 0 || length > MAX_PATH)
		return L"C:\\Windows\\System32";
	return std::wstring(buffer, length);
}

std::wstring joinPath(const std::wstring& a, const std::wstring& b)
{
	if (a.empty())
		return b;
	wchar_t last = a.back();
	if (last == L'\\' || last == L'/')
		return a + b;
	return a + L"\\" + b;
}

bool fileExists(const std::wstring& path)
{
	DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool directoryExists(const std::wstring& path)
{
	DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool createDirectoryRecursive(const std::wstring& path)
{
	if (path.empty() || directoryExists(path))
		return true;
	size_t slash = path.find_last_of(L"\\/");
	if (slash != std::wstring::npos && slash > 0)
	{
		if (!createDirectoryRecursive(path.substr(0, slash)))
			return false;
	}
	return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

void logLine(const wchar_t* level, const wchar_t* format, ...)
{
	wchar_t buffer[1024];
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(buffer, _TRUNCATE, format, args);
	va_end(args);
	fwprintf(stderr, L"[ApoRegistration] %s: %s\n", level, buffer);
	fflush(stderr);
}
}

int ApoRegistration::waitForProcess(const std::wstring& executable, const std::wstring& arguments, unsigned timeoutMs)
{
	std::wstring commandLine = L"\"" + executable + L"\" " + arguments;
	std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back(L'\0');

	STARTUPINFOW startupInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.dwFlags = STARTF_USESHOWWINDOW;
	startupInfo.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION processInfo;
	ZeroMemory(&processInfo, sizeof(processInfo));

	if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo))
	{
		logLine(L"ERR", L"CreateProcess failed for %s (gle=%lu)", executable.c_str(), GetLastError());
		return -1;
	}

	DWORD waitResult = WaitForSingleObject(processInfo.hProcess, timeoutMs);
	DWORD exitCode = static_cast<DWORD>(-1);
	if (waitResult == WAIT_OBJECT_0)
	{
		GetExitCodeProcess(processInfo.hProcess, &exitCode);
	}
	else
	{
		logLine(L"ERR", L"%s timed out after %u ms", executable.c_str(), timeoutMs);
		TerminateProcess(processInfo.hProcess, 1);
	}

	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);
	return static_cast<int>(exitCode);
}

ApoRegistration::Result ApoRegistration::install(const std::wstring& installDir)
{
	std::wstring dllPath = joinPath(installDir, L"EqualizerAPO.dll");
	if (!fileExists(dllPath))
	{
		logLine(L"ERR", L"EqualizerAPO.dll not found at %s", dllPath.c_str());
		return Result::DllNotFound;
	}

	try
	{
		RegistryHelper::createKey(kRegPath);
		RegistryHelper::writeValue(kRegPath, L"InstallPath", installDir);

		std::wstring configDir = joinPath(installDir, L"config");
		createDirectoryRecursive(configDir);

		if (!RegistryHelper::valueExists(kRegPath, L"ConfigPath"))
			RegistryHelper::writeValue(kRegPath, L"ConfigPath", configDir);

		if (!RegistryHelper::valueExists(kRegPath, L"EnableTrace"))
			RegistryHelper::writeValue(kRegPath, L"EnableTrace", L"false");

		RegistryHelper::writeDWORDValue(kAudioRegPath, L"DisableProtectedAudioDG", 1);
	}
	catch (const RegistryException& e)
	{
		logLine(L"ERR", L"Registry write failed: %s", e.getMessage().c_str());
		return Result::RegistryFailed;
	}

	std::wstring regsvr32 = joinPath(systemPath(), L"regsvr32.exe");
	std::wstring registerArgs = L"/s \"" + joinPath(installDir, L"EqualizerAPO.dll") + L"\"";
	int rc = waitForProcess(regsvr32, registerArgs, 30000);
	if (rc != 0)
	{
		logLine(L"ERR", L"regsvr32 returned %d", rc);
		return Result::RegistrationFailed;
	}

	std::wstring configDir = joinPath(installDir, L"config");
	std::wstring icacls = joinPath(systemPath(), L"icacls.exe");
	std::wstring aclArgs = L"\"" + configDir + L"\" /grant *S-1-5-32-545:(OI)(CI)F /T /C /Q";
	rc = waitForProcess(icacls, aclArgs, 30000);
	if (rc != 0)
		logLine(L"WARN", L"icacls returned %d, continuing", rc);

	return Result::Success;
}

ApoRegistration::Result ApoRegistration::uninstall(const std::wstring& installDir)
{
	bool serviceWasRunning = stopAudioService();

	Result deviceResult = Result::Success;
	for (int inputPass = 0; inputPass <= 1; inputPass++)
	{
		std::vector<std::shared_ptr<AbstractAPOInfo>> apoInfos = DeviceAPOInfo::loadAllInfos(inputPass == 1);
		for (std::shared_ptr<AbstractAPOInfo>& apoInfo : apoInfos)
		{
			try
			{
				if (apoInfo->isInstalled())
					apoInfo->uninstall();
			}
			catch (const RegistryException& e)
			{
				logLine(L"ERR", L"Failed to uninstall APO from device: %s", e.getMessage().c_str());
				deviceResult = Result::DeviceUninstallFailed;
			}
			catch (const DeviceException& e)
			{
				logLine(L"ERR", L"Failed to uninstall APO from device: %s", e.getMessage().c_str());
				deviceResult = Result::DeviceUninstallFailed;
			}
		}
	}

	std::wstring dllPath = joinPath(installDir, L"EqualizerAPO.dll");
	if (fileExists(dllPath))
	{
		std::wstring regsvr32 = joinPath(systemPath(), L"regsvr32.exe");
		std::wstring unregisterArgs = L"/u /s \"" + dllPath + L"\"";
		int rc = waitForProcess(regsvr32, unregisterArgs, 30000);
		if (rc != 0)
			logLine(L"WARN", L"regsvr32 /u returned %d, continuing", rc);
	}

	try
	{
		if (RegistryHelper::valueExists(kAudioRegPath, L"DisableProtectedAudioDG"))
			RegistryHelper::deleteValue(kAudioRegPath, L"DisableProtectedAudioDG");
	}
	catch (const RegistryException& e)
	{
		logLine(L"WARN", L"Failed to clean DisableProtectedAudioDG: %s", e.getMessage().c_str());
	}

	try
	{
		if (RegistryHelper::keyExists(kRegPath) && RegistryHelper::keyEmpty(kRegPath))
			RegistryHelper::deleteKey(kRegPath);
	}
	catch (const RegistryException& e)
	{
		logLine(L"WARN", L"Failed to remove EqualizerAPO registry key: %s", e.getMessage().c_str());
	}

	if (serviceWasRunning)
		startAudioService();

	return deviceResult;
}

bool ApoRegistration::stopAudioService()
{
	SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (manager == nullptr)
	{
		logLine(L"ERR", L"OpenSCManager failed: %lu", GetLastError());
		return false;
	}

	try
	{
		Service service(manager, kAudioServiceName, true);
		DWORD state = service.getState();
		if (state == SERVICE_RUNNING)
		{
			service.stop();
			CloseServiceHandle(manager);
			logLine(L"INFO", L"Stopped AudioSrv");
			return true;
		}
		CloseServiceHandle(manager);
		return false;
	}
	catch (const ServiceException& e)
	{
		CloseServiceHandle(manager);
		logLine(L"ERR", L"Failed to stop AudioSrv: %s", e.getMessage().c_str());
		return false;
	}
}

bool ApoRegistration::startAudioService()
{
	SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (manager == nullptr)
	{
		logLine(L"ERR", L"OpenSCManager failed: %lu", GetLastError());
		return false;
	}

	try
	{
		Service service(manager, kAudioServiceName, true);
		DWORD state = service.getState();
		if (state == SERVICE_STOPPED)
		{
			service.start();
			CloseServiceHandle(manager);
			logLine(L"INFO", L"Started AudioSrv");
			return true;
		}
		CloseServiceHandle(manager);
		return state == SERVICE_RUNNING;
	}
	catch (const ServiceException& e)
	{
		CloseServiceHandle(manager);
		logLine(L"ERR", L"Failed to start AudioSrv: %s", e.getMessage().c_str());
		return false;
	}
}

std::wstring ApoRegistration::detectLegacyInstall()
{
	try
	{
		if (!RegistryHelper::valueExists(kRegPath, L"InstallPath"))
			return std::wstring();
		std::wstring legacy = RegistryHelper::readValue(kRegPath, L"InstallPath");
		if (legacy.empty())
			return std::wstring();
		std::wstring marker = joinPath(legacy, L"Uninstall.exe");
		if (!fileExists(marker))
			return std::wstring();
		return legacy;
	}
	catch (const RegistryException&)
	{
		return std::wstring();
	}
}

bool ApoRegistration::migrateLegacyConfig(const std::wstring& legacyDir, const std::wstring& newDir)
{
	std::wstring legacyConfig = joinPath(legacyDir, L"config");
	std::wstring newConfig = joinPath(newDir, L"config");

	if (!directoryExists(legacyConfig))
		return false;
	if (!createDirectoryRecursive(newConfig))
		return false;

	bool copiedAny = false;
	WIN32_FIND_DATAW findData;
	std::wstring pattern = joinPath(legacyConfig, L"*");
	HANDLE finder = FindFirstFileW(pattern.c_str(), &findData);
	if (finder == INVALID_HANDLE_VALUE)
		return false;

	do
	{
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		std::wstring source = joinPath(legacyConfig, findData.cFileName);
		std::wstring target = joinPath(newConfig, findData.cFileName);
		if (fileExists(target))
			continue;
		if (CopyFileW(source.c_str(), target.c_str(), TRUE))
			copiedAny = true;
	}
	while (FindNextFileW(finder, &findData));

	FindClose(finder);
	return copiedAny;
}
