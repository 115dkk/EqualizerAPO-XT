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
#include <shlobj.h>
#include <knownfolders.h>
#include <objbase.h>
#include <objidl.h>

#include "devices/AbstractAPOInfo.h"
#include "AudioEngineAccess.h"
#include "ComPtr.h"
#include "devices/DeviceAPOInfo.h"
#include "LogHelper.h"
#include "RegistryHelper.h"
#include "ServiceHelper.h"
#include "Win32Resource.h"

namespace
{
constexpr wchar_t kRegPath[] = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO";
constexpr wchar_t kAudioRegPath[] = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio";
constexpr wchar_t kAudioServiceName[] = L"AudioSrv";
constexpr wchar_t kAudioEndpointBuilderServiceName[] = L"AudioEndpointBuilder";

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

ApoRegistration::Result ApoRegistration::install(const std::wstring& installDir)
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
	if (!createStartMenuShortcuts(installDir))
		logLine(L"WARN", L"Failed to create DeviceSelector start menu shortcut");

	return Result::Success;
}

ApoRegistration::Result ApoRegistration::uninstall(const std::wstring& installDir)
{
	if (!AudioEngineAccess::isElevated())
		logLine(L"WARN", L"uninstall() is running unelevated; device APO removal and HKLM cleanup will fail");

	bool serviceWasRunning = stopAudioService();

	const Result deviceResult = uninstallAllDeviceApos([](const std::wstring& message) {
		logLine(L"ERR", L"Failed to uninstall APO from device: %s", message.c_str());
	});

	std::wstring dllPath = joinPath(installDir, L"EqualizerAPO.dll");
	if (fileExists(dllPath))
	{
		int rc = registerComServer(dllPath, true);
		if (rc != 0)
			logLine(L"WARN", L"DllUnregisterServer returned 0x%08X, continuing", rc);
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

	if (!removeStartMenuShortcuts())
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

ApoRegistration::Result ApoRegistration::uninstallAllDeviceApos(const DeviceUninstallErrorSink& errorSink)
{
	Result result = Result::Success;
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

namespace
{
constexpr wchar_t kShortcutFolderName[] = L"EqualizerAPO-XT";
constexpr wchar_t kDeviceSelectorShortcutFile[] = L"Device Selector.lnk";

std::wstring publicProgramsPath()
{
	winutil::UniqueCoTaskMemPtr<wchar_t> raw;
	HRESULT hr = SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, raw.put());
	if (FAILED(hr) || !raw)
		return std::wstring();
	std::wstring path(raw.get());
	return path;
}

HRESULT writeShellLink(const std::wstring& target, const std::wstring& workingDir,
	const std::wstring& description, const std::wstring& iconPath, int iconIndex,
	const std::wstring& linkPath)
{
	winutil::ComPtr<IShellLinkW> shellLink;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
		IID_IShellLinkW, reinterpret_cast<void**>(shellLink.put()));
	if (FAILED(hr) || !shellLink)
		return hr;

	shellLink->SetPath(target.c_str());
	if (!workingDir.empty())
		shellLink->SetWorkingDirectory(workingDir.c_str());
	if (!description.empty())
		shellLink->SetDescription(description.c_str());
	if (!iconPath.empty())
		shellLink->SetIconLocation(iconPath.c_str(), iconIndex);

	winutil::ComPtr<IPersistFile> persistFile;
	hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(persistFile.put()));
	if (SUCCEEDED(hr) && persistFile)
	{
		hr = persistFile->Save(linkPath.c_str(), TRUE);
	}
	return hr;
}
} // namespace

bool ApoRegistration::createStartMenuShortcuts(const std::wstring& installDir)
{
	std::wstring deviceSelector = joinPath(installDir, L"DeviceSelector.exe");
	if (!fileExists(deviceSelector))
	{
		logLine(L"WARN", L"DeviceSelector.exe not found at %s", deviceSelector.c_str());
		return false;
	}

	std::wstring programsDir = publicProgramsPath();
	if (programsDir.empty())
	{
		logLine(L"ERR", L"SHGetKnownFolderPath(FOLDERID_CommonPrograms) failed");
		return false;
	}

	std::wstring shortcutFolder = joinPath(programsDir, kShortcutFolderName);
	if (!createDirectoryRecursive(shortcutFolder))
	{
		logLine(L"ERR", L"Failed to create %s", shortcutFolder.c_str());
		return false;
	}

	winutil::ComApartment apartment(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (!apartment.isUsable())
	{
		logLine(L"ERR", L"COM initialization failed (hr=0x%08lx)",
			static_cast<unsigned long>(apartment.status()));
		return false;
	}

	std::wstring linkPath = joinPath(shortcutFolder, kDeviceSelectorShortcutFile);
	HRESULT hr = writeShellLink(deviceSelector, installDir,
		L"Configure which audio devices use EqualizerAPO",
		deviceSelector, 0, linkPath);

	if (FAILED(hr))
	{
		logLine(L"ERR", L"Failed to write %s (hr=0x%08lx)", linkPath.c_str(), static_cast<unsigned long>(hr));
		return false;
	}
	logLine(L"INFO", L"Wrote shortcut %s", linkPath.c_str());
	return true;
}

bool ApoRegistration::removeStartMenuShortcuts()
{
	std::wstring programsDir = publicProgramsPath();
	if (programsDir.empty())
		return false;

	std::wstring shortcutFolder = joinPath(programsDir, kShortcutFolderName);
	std::wstring linkPath = joinPath(shortcutFolder, kDeviceSelectorShortcutFile);

	bool ok = true;
	if (fileExists(linkPath) && !DeleteFileW(linkPath.c_str()))
	{
		logLine(L"WARN", L"DeleteFile failed for %s (gle=%lu)", linkPath.c_str(), GetLastError());
		ok = false;
	}

	// Best-effort cleanup of empty folder; ignore failure (other lnks may live there).
	RemoveDirectoryW(shortcutFolder.c_str());
	return ok;
}
