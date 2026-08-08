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

#include "devices/AbstractAPOInfo.h"
#include "PathHelper.h"
#include "AudioEngineAccess.h"
#include "devices/DeviceAPOInfo.h"
#include "LogHelper.h"
#include "RegistryHelper.h"
#include "ServiceHelper.h"
#include "StartMenuShortcuts.h"
#include "Win32Resource.h"

namespace
{
// The one spelling lives in RegistryHelper.h; DeviceAPOInfoKeys.h composes
// on the same macro.
constexpr const wchar_t* kRegPath = APP_REGPATH;
constexpr wchar_t kAudioRegPath[] = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio";
constexpr wchar_t kAudioServiceName[] = L"AudioSrv";
constexpr wchar_t kAudioEndpointBuilderServiceName[] = L"AudioEndpointBuilder";

// Audit #250 F018: the shared path vocabulary lives in PathHelper.h.
using pathutil::joinPath;
using pathutil::fileExists;
using pathutil::createDirectoryRecursive;

void logLine(const wchar_t* level, const wchar_t* format, ...)
{
	wchar_t buffer[1024];
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(buffer, _TRUNCATE, format, args);
	va_end(args);
	LogFStatic(L"[ApoRegistration] %s: %s", level, buffer);
}
}

int ApoRegistration::registerComServer(const std::wstring& dllPath, bool unregister)
{
	// LOAD_WITH_ALTERED_SEARCH_PATH resolves EqualizerAPO.dll's own dependencies
	// (FFTW, libsndfile, ...) relative to the DLL directory, matching how the
	// audio engine and regsvr32 load it.
	winutil::UniqueModule module(LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
	if (!module)
	{
		logLine(L"ERR", L"LoadLibrary failed for %s (gle=%lu)", dllPath.c_str(), GetLastError());
		return -1;
	}

	using DllServerProc = HRESULT(__stdcall*)();
	const char* entryName = unregister ? "DllUnregisterServer" : "DllRegisterServer";
	DllServerProc proc = reinterpret_cast<DllServerProc>(GetProcAddress(module.get(), entryName));

	HRESULT hr = E_FAIL;
	if (proc != nullptr)
		hr = proc();
	else
		logLine(L"ERR", L"%S not found in %s (gle=%lu)", entryName, dllPath.c_str(), GetLastError());

	return SUCCEEDED(hr) ? 0 : static_cast<int>(hr);
}

ApoRegistration::Result ApoRegistration::writeAppInstallRegistry(
	const std::wstring& installDir, const std::wstring& configDir,
	IRegistry& registry)
{
	try
	{
		registry.createKey(kRegPath);
		registry.writeValue(kRegPath, L"InstallPath", installDir);

		// Audit #250 F034: an ignored failure here used to report Success
		// while ConfigPath pointed at a directory that does not exist -
		// "installed but doing nothing".
		if (!createDirectoryRecursive(configDir))
		{
			logLine(L"ERR", L"Could not create config directory %s",
				configDir.c_str());
			return Result::RegistryFailed;
		}

		if (!registry.valueExists(kRegPath, L"ConfigPath"))
			registry.writeValue(kRegPath, L"ConfigPath", configDir);

		if (!registry.valueExists(kRegPath, L"EnableTrace"))
			registry.writeValue(kRegPath, L"EnableTrace", L"false");

		registry.writeDWORDValue(kAudioRegPath, L"DisableProtectedAudioDG", 1);
	}
	catch (const RegistryException& e)
	{
		logLine(L"ERR", L"Registry write failed: %s", e.getMessage().c_str());
		return Result::RegistryFailed;
	}
	return Result::Success;
}

void ApoRegistration::cleanupAppRegistry(IRegistry& registry)
{
	try
	{
		if (registry.valueExists(kAudioRegPath, L"DisableProtectedAudioDG"))
			registry.deleteValue(kAudioRegPath, L"DisableProtectedAudioDG");
	}
	catch (const RegistryException& e)
	{
		logLine(L"WARN", L"Failed to clean DisableProtectedAudioDG: %s", e.getMessage().c_str());
	}

	try
	{
		if (registry.keyExists(kRegPath) && registry.keyEmpty(kRegPath))
			registry.deleteKey(kRegPath);
	}
	catch (const RegistryException& e)
	{
		logLine(L"WARN", L"Failed to remove EqualizerAPO registry key: %s", e.getMessage().c_str());
	}
}

ApoRegistration::Result ApoRegistration::install(const std::wstring& installDir,
	IRegistry& registry)
{
	// Both hooks write HKLM and rewrite ACLs on the install tree, so they only
	// work elevated. That was an assumption nothing checked: an unelevated run
	// failed later, one operation at a time, with a registry error that says
	// nothing about elevation. Say it once, at the top, and keep going - the
	// hooks are invoked by the installer and a refusal here would break an
	// install that Windows may still be able to complete.
	if (!AudioEngineAccess::isElevated())
		logLine(L"WARN", L"install() is running unelevated; HKLM writes and ACL grants will fail");

	std::wstring dllPath = joinPath(installDir, L"EqualizerAPO.dll");
	if (!fileExists(dllPath))
	{
		logLine(L"ERR", L"EqualizerAPO.dll not found at %s", dllPath.c_str());
		return Result::DllNotFound;
	}

	const Result registryResult = writeAppInstallRegistry(installDir,
		joinPath(installDir, L"config"), registry);
	if (registryResult != Result::Success)
		return registryResult;

	// audiodg.exe loads EqualizerAPO.dll via the COM InprocServer32 path written
	// below, and when Velopack installs the app under %LocalAppData% the default
	// ACL only grants the installing user and Administrators - so the audio engine
	// cannot map the DLL and the symptom the user sees is GetMixFormat or
	// Initialize returning E_ACCESSDENIED in DeviceSelector. Widen the tree before
	// any APO registration takes effect. Who gets what, and the trust boundary the
	// grant relies on, are in helpers/AudioEngineAccess.cpp.
	AudioEngineAccess::Grant installGrant = AudioEngineAccess::grantEngineAccess(installDir);
	if (installGrant != AudioEngineAccess::Grant::Applied)
		logLine(L"WARN", L"Install root access grant %s, continuing", AudioEngineAccess::describe(installGrant));

	int rc = registerComServer(dllPath, false);
	if (rc != 0)
	{
		logLine(L"ERR", L"DllRegisterServer returned 0x%08X", rc);
		return Result::RegistrationFailed;
	}

	// secureConfigDir already logs which grant failed.
	secureConfigDir(joinPath(installDir, L"config"));

	// Velopack's vpk pack only emits a shortcut for --mainExe (Editor.exe).
	// DeviceSelector is the elevated companion that performs per-device APO
	// install/uninstall, so it needs its own Start Menu entry. We create it
	// here in the install hook because the hook runs elevated and can write
	// to the Public Programs folder.
	if (!StartMenuShortcuts::create(installDir))
		logLine(L"WARN", L"Failed to create DeviceSelector start menu shortcut");

	return Result::Success;
}

ApoRegistration::Result ApoRegistration::uninstall(const std::wstring& installDir,
	IRegistry& registry)
{
	if (!AudioEngineAccess::isElevated())
		logLine(L"WARN", L"uninstall() is running unelevated; device APO removal and HKLM cleanup will fail");

	bool serviceWasRunning = stopAudioService();

	const Result deviceResult = uninstallAllDeviceApos([](const std::wstring& message) {
		logLine(L"ERR", L"Failed to uninstall APO from device: %s", message.c_str());
	}, registry);

	std::wstring dllPath = joinPath(installDir, L"EqualizerAPO.dll");
	if (fileExists(dllPath))
	{
		int rc = registerComServer(dllPath, true);
		if (rc != 0)
			logLine(L"WARN", L"DllUnregisterServer returned 0x%08X, continuing", rc);
	}

	cleanupAppRegistry(registry);

	if (!StartMenuShortcuts::remove())
		logLine(L"WARN", L"Failed to remove start menu shortcuts");

	if (serviceWasRunning)
	{
		// This epilogue intentionally belongs to the package hook, not the /u
		// helper. The hook has already stopped AudioSrv and must rebuild the
		// endpoint graph exactly once after every device has been cleaned.
		try
		{
			ServiceHelper::restartService(kAudioEndpointBuilderServiceName);
		}
		catch (const ServiceException& e)
		{
			logLine(L"WARN", L"Failed to restart AudioEndpointBuilder; a reboot may be needed to fully apply the removal: %s", e.getMessage().c_str());
		}
		startAudioService();
	}

	return deviceResult;
}

ApoRegistration::Result ApoRegistration::uninstallAllDeviceApos(const DeviceUninstallErrorSink& errorSink,
	IRegistry& registry)
{
	Result result = Result::Success;
	for (int inputPass = 0; inputPass <= 1; inputPass++)
	{
		std::vector<std::shared_ptr<AbstractAPOInfo>> apoInfos = DeviceAPOInfo::loadAllInfos(inputPass == 1, registry);
		for (std::shared_ptr<AbstractAPOInfo>& apoInfo : apoInfos)
		{
			try
			{
				if (apoInfo->isInstalled())
					apoInfo->uninstall();
			}
			catch (const RegistryException& e)
			{
				if (errorSink)
					errorSink(e.getMessage());
				result = Result::DeviceUninstallFailed;
			}
			catch (const DeviceException& e)
			{
				if (errorSink)
					errorSink(e.getMessage());
				result = Result::DeviceUninstallFailed;
			}
		}
	}
	return result;
}

bool ApoRegistration::stopAudioService()
{
	winutil::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
	if (!manager)
	{
		logLine(L"ERR", L"OpenSCManager failed: %lu", GetLastError());
		return false;
	}

	try
	{
		Service service(manager.get(), kAudioServiceName, true);
		DWORD state = service.getState();
		if (state == SERVICE_RUNNING)
		{
			service.stop();
			logLine(L"INFO", L"Stopped AudioSrv");
			return true;
		}
		return false;
	}
	catch (const ServiceException& e)
	{
		logLine(L"ERR", L"Failed to stop AudioSrv: %s", e.getMessage().c_str());
		return false;
	}
}

bool ApoRegistration::startAudioService()
{
	winutil::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
	if (!manager)
	{
		logLine(L"ERR", L"OpenSCManager failed: %lu", GetLastError());
		return false;
	}

	try
	{
		Service service(manager.get(), kAudioServiceName, true);
		DWORD state = service.getState();
		if (state == SERVICE_STOPPED)
		{
			service.start();
			logLine(L"INFO", L"Started AudioSrv");
			return true;
		}
		return state == SERVICE_RUNNING;
	}
	catch (const ServiceException& e)
	{
		logLine(L"ERR", L"Failed to start AudioSrv: %s", e.getMessage().c_str());
		return false;
	}
}

bool ApoRegistration::secureConfigDir(const std::wstring& configDir)
{
	// Who needs what on a config directory is declared once, in
	// helpers/AudioEngineAccess.cpp, together with the check that verifies it.
	const AudioEngineAccess::Grant grant = AudioEngineAccess::grantConfigAccess(configDir);
	if (grant != AudioEngineAccess::Grant::Applied)
		logLine(L"WARN", L"Config directory access grant %s", AudioEngineAccess::describe(grant));
	return grant == AudioEngineAccess::Grant::Applied;
}

