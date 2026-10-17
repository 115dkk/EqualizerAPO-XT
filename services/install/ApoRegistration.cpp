/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2025  EqualizerAPO-XT contributors
    SPDX-License-Identifier: GPL-2.0-or-later

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

#include "services/install/ApoRegistration.h"
#include "services/install/InstallWaitBudget.h"
#include "services/registry/RegistryPaths.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include "devices/AbstractAPOInfo.h"
#include "platform/windows/WindowsPath.h"
#include "services/security/AudioEngineAccess.h"
#include "devices/DeviceAPOInfo.h"
#include "devices/DeviceAPOInfoKeys.h"
#include "devices/AsioAPOInfo.h"
#include "devices/VoicemeeterAPOInfo.h"
#include "runtime/lifetime/ScopeExit.h"
#include "services/logging/Logging.h"
#include "services/logging/TaggedLogger.h"
#include "services/registry/WindowsRegistry.h"
#include "services/windows/WindowsService.h"
#include "services/shell/StartMenuShortcuts.h"
#include "platform/windows/ComSelfRegistration.h"
#include "platform/windows/GuidText.h"

using std::wstring;

namespace
{
// The one spelling lives in services/registry/RegistryPaths.h;
// DeviceAPOInfoKeys.h composes on the same macro.
constexpr const wchar_t* kRegPath = APP_REGPATH;
constexpr wchar_t kAudioEndpointBuilderServiceName[] = L"AudioEndpointBuilder";

// Audit #250 F018: the shared path vocabulary lives in WindowsPath.h.
using pathutil::joinPath;
using pathutil::fileExists;
using pathutil::createDirectoryRecursive;

constexpr logging::TaggedLogger logLine(L"ApoRegistration");
}

int ApoRegistration::registerComServer(const std::wstring& dllPath, bool unregister)
{
	// Both install and recovery use this helper, which logs load, lookup
	// and entry-point failures itself.
	const HRESULT hr = winutil::selfRegisterComServer(L"ApoRegistration", dllPath, unregister);
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

		if (!configDir.empty() && !registry.valueExists(kRegPath, L"ConfigPath"))
			registry.writeValue(kRegPath, L"ConfigPath", configDir);

		if (!registry.valueExists(kRegPath, L"EnableTrace"))
			registry.writeValue(kRegPath, L"EnableTrace", L"false");

		checkProtectedAudioDG(true, registry);
	}
	catch (const RegistryError& e)
	{
		logLine(L"ERR", L"Registry write failed: %s", e.getMessage().c_str());
		return Result::RegistryFailed;
	}
	return Result::Success;
}

ApoRegistration::Result ApoRegistration::writeMissingConfigFallback(const std::wstring& installDir,
	IRegistry& registry)
{
	try
	{
		if (registry.keyExists(kRegPath) && registry.valueExists(kRegPath, L"ConfigPath"))
			return Result::Success;
	}
	catch (const RegistryError& e)
	{
		// An unreadable value is not evidence that it is absent.
		logLine(L"ERR", L"Could not check ConfigPath after migration: %s", e.getMessage().c_str());
		return Result::RegistryFailed;
	}
	// The existing role checks absence again before writing. No caller-supplied
	// profile directory enters this fallback.
	return writeAppInstallRegistry(installDir, joinPath(installDir, L"config"), registry);
}

void ApoRegistration::cleanupAppRegistry(IRegistry& registry)
{
	try
	{
		if (registry.valueExists(protectedDGKeyPath, protectedDGValueName))
			registry.deleteValue(protectedDGKeyPath, protectedDGValueName);
	}
	catch (const RegistryError& e)
	{
		logLine(L"WARN", L"Failed to clean DisableProtectedAudioDG: %s", e.getMessage().c_str());
	}

	try
	{
		if (registry.keyExists(kRegPath) && registry.keyEmpty(kRegPath))
			registry.deleteKey(kRegPath);
	}
	catch (const RegistryError& e)
	{
		logLine(L"WARN", L"Failed to remove EqualizerAPO registry key: %s", e.getMessage().c_str());
	}
}

ApoRegistration::Result ApoRegistration::install(const std::wstring& installDir, const std::wstring& configDir,
	IRegistry& registry, bool installGrantsPrepared)
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
		configDir, registry);
	if (registryResult != Result::Success)
		return registryResult;

	// audiodg.exe loads EqualizerAPO.dll via the COM InprocServer32 path written
	// below, and when Velopack installs the app under %LocalAppData% the default
	// ACL only grants the installing user and Administrators - so the audio engine
	// cannot map the DLL and the symptom the user sees is GetMixFormat or
	// Initialize returning E_ACCESSDENIED in DeviceSelector. Widen the tree before
	// any APO registration takes effect. Who gets what, and the trust boundary the
	// grant relies on, are in services/security/AudioEngineAccess.cpp.
	// With no unelevated parent, retain today's grants: Velopack was launched
	// elevated and installed into that account's own LocalAppData. This assumes
	// that tree is not writable by another standard user. A prepared hand-off
	// skips both recursive grants below; its user already applied the ACEs.
	if (shouldGrantInstallAccess(installGrantsPrepared))
	{
		AudioEngineAccess::Grant installGrant = AudioEngineAccess::grantEngineAccess(installDir);
		if (installGrant != AudioEngineAccess::Grant::Applied)
			logLine(L"WARN", L"Install root access grant %s, continuing", AudioEngineAccess::describe(installGrant));
	}

	// Refresh even an existing registration: a new DLL may publish new COM
	// metadata while keeping the same file path and CLSIDs.
	const auto verification = verify(registry, true, true);
	if (verification.repairResult != Result::Success)
		return verification.repairResult;

	// secureConfigDir already logs which grant failed.
	if (shouldGrantInstallAccess(installGrantsPrepared))
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

	const bool serviceWasRunning = stopAudioService();
	// The audio service comes back on every path out of this function,
	// including an exception thrown by anything below: a hook that ends with
	// AudioSrv stopped leaves the user without sound until a reboot (audit
	// #348 TD-02).
	SCOPE_EXIT{
		if (!serviceWasRunning)
			return;
		try
		{
			// This epilogue intentionally belongs to the package hook, not
			// the /u helper. The hook has already stopped AudioSrv and must
			// rebuild the endpoint graph exactly once after every device has
			// been cleaned.
			try
			{
				WindowsServiceControl::restart(kAudioEndpointBuilderServiceName, installwait::remaining(4000));
			}
			catch (const WindowsServiceError& e)
			{
				logLine(L"WARN", L"Failed to restart AudioEndpointBuilder; a reboot may be needed to fully apply the removal: %s", e.getMessage().c_str());
			}
			startAudioService();
		}
		catch (...)
		{
			logLine(L"ERR", L"Restarting the audio services after the uninstall failed");
		}
	};

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

	return deviceResult;
}

ApoRegistration::Result ApoRegistration::uninstallAllDeviceApos(const DeviceUninstallErrorSink& errorSink,
	IRegistry& registry, const EndpointProbe& probe)
{
	Result result = Result::Success;
	auto report = [&](const std::wstring& message) {
		if (errorSink)
			errorSink(message);
		result = Result::DeviceUninstallFailed;
	};
	// One item at a time, each in its own try: DeviceAPOInfo::loadAllInfos
	// would throw for the whole list on the first unreadable endpoint.
	auto guarded = [&](const std::wstring& what, const auto& step) {
		try
		{
			step();
		}
		catch (const WideError& e)
		{
			report(what.empty() ? e.getMessage() : what + L": " + e.getMessage());
		}
	};
	auto uninstallInfo = [&](const std::wstring& what, AbstractAPOInfo& info) {
		guarded(what, [&] {
			if (info.isInstalled())
				info.uninstall();
		});
	};

	for (int inputPass = 0; inputPass <= 1; inputPass++)
	{
		const bool input = inputPass == 1;
		const std::wstring defaultDeviceGuid = probe.defaultEndpointGuid(input);

		// A direction with no endpoint key at all has nothing to clean.
		const std::wstring endpointRoot = input ? captureKeyPath : renderKeyPath;
		std::vector<std::wstring> endpointGuids;
		guarded(L"", [&] {
			if (registry.keyExists(endpointRoot))
				endpointGuids = registry.enumSubKeys(endpointRoot);
		});
		for (const std::wstring& endpointGuid : endpointGuids)
		{
			guarded(endpointGuid, [&] {
				DeviceAPOInfo info(registry, probe);
				if (info.load(endpointGuid, defaultDeviceGuid))
				{
					// As loadAllInfos does: the selection starts as what is
					// installed.
					info.getSelectedInstallState() = info.getCurrentInstallState();
					uninstallInfo(endpointGuid, info);
				}
			});
		}

		std::vector<std::shared_ptr<AbstractAPOInfo>> others;
		if (!input)
			guarded(L"Voicemeeter", [&] { VoicemeeterAPOInfo::prependInfos(others, registry); });
		guarded(L"ASIO", [&] { AsioAPOInfo::appendInfos(others, input, registry); });
		for (std::shared_ptr<AbstractAPOInfo>& apoInfo : others)
			uninstallInfo(L"", *apoInfo);
	}
	return result;
}

// Velopack allows about 15 seconds per hook. Service waits use at most
// 3 s (AudioSrv stop) + 4 s (uninstall's endpoint-builder restart) + 3 s
// (AudioSrv start) = 10 s. Updated uses 6 s; obsolete uses 3 s. Do not
// give each service in a restart its own deadline. The hook's shared 12 s
// budget also caps these waits when ACL subprocesses already used time.
bool ApoRegistration::stopAudioService()
{
	bool active = false;
	try
	{
		WindowsServiceControl::stop(audioServiceName, installwait::remaining(3000), &active);
		logLine(L"INFO", L"Stopped AudioSrv");
		return active;
	}
	catch (const WindowsServiceError& e)
	{
		logLine(L"ERR", L"Failed to stop AudioSrv: %s", e.getMessage().c_str());
		return active;
	}
}

bool ApoRegistration::startAudioService()
{
	try
	{
		WindowsServiceControl::start(audioServiceName, installwait::remaining(3000));
		logLine(L"INFO", L"Started AudioSrv");
		return true;
	}
	catch (const WindowsServiceError& e)
	{
		logLine(L"ERR", L"Failed to start AudioSrv: %s", e.getMessage().c_str());
		return false;
	}
}

bool ApoRegistration::secureConfigDir(const std::wstring& configDir)
{
	// Who needs what on a config directory is declared once, in
	// services/security/AudioEngineAccess.cpp, together with the check that verifies it.
	const AudioEngineAccess::Grant grant = AudioEngineAccess::grantConfigAccess(configDir);
	if (grant != AudioEngineAccess::Grant::Applied)
		logLine(L"WARN", L"Config directory access grant %s", AudioEngineAccess::describe(grant));
	return grant == AudioEngineAccess::Grant::Applied;
}


bool ApoRegistration::checkProtectedAudioDG(bool fix, IRegistry& registry)
{
	bool result = true;

	if (!registry.valueExists(protectedDGKeyPath, protectedDGValueName) || registry.readDWORDValue(protectedDGKeyPath, protectedDGValueName) != 1)
	{
		result = false;

		if (fix)
			registry.writeDWORDValue(protectedDGKeyPath, protectedDGValueName, 1);
	}

	return result;
}

ApoRegistration::Verification ApoRegistration::verify(IRegistry& registry, bool fix, bool refreshRegistration)
{
	Verification result;
	result.protectedAudioDG = checkProtectedAudioDG(fix, registry);
	const auto pre = winutil::guidToString(EQUALIZERAPO_PRE_MIX_GUID);
	const auto post = winutil::guidToString(EQUALIZERAPO_POST_MIX_GUID);
	result.registration = registry.keyExists(apoRegistrationKeyPath L"\\" + pre)
		&& registry.keyExists(apoRegistrationKeyPath L"\\" + post)
		&& registry.keyExists(clsidKeyPath L"\\" + pre)
		&& registry.keyExists(clsidKeyPath L"\\" + post);
	if (fix && (!result.registration || refreshRegistration))
	{
		const auto directory = registry.valueExists(APP_REGPATH, L"InstallPath")
			? registry.readValue(APP_REGPATH, L"InstallPath") : pathutil::exeDirectory();
		if (directory.empty())
		{
			logLine(L"ERR", L"Could not resolve the APO installation directory");
			result.repairResult = Result::DllNotFound;
		}
		else if (registerComServer(joinPath(directory, L"EqualizerAPO.dll"), false) != 0)
			result.repairResult = Result::RegistrationFailed;
	}
	// The flags describe the state found, so recovery callers retain their
	// existing notification behavior. repairResult records a failed repair.
	return result;
}
