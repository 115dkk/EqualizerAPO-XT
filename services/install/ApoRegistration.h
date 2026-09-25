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

#include <functional>
#include <string>

#include "services/registry/IRegistry.h"

class ApoRegistration
{
public:
	enum class Result
	{
		Success = 0,
		RegistrationFailed = 1,
		RegistryFailed = 2,
		AclFailed = 3,
		ServiceFailed = 4,
		DeviceUninstallFailed = 5,
		DllNotFound = 6
	};

	// Audit #250 A3/F002: every registry access goes through the injected
	// port, defaulting to the live adapter so existing callers keep
	// compiling unchanged - the same shape DeviceAPOInfo uses. The service
	// restarts, ACL grants, COM self-registration and shortcut writing are
	// outside the port by design; tests judge the registry role through the
	// two functions below and the device sweep, not the whole hook.
	static Result install(const std::wstring& installDir,
		IRegistry& registry = systemRegistry(), bool installGrantsPrepared = false);
	// A prepared hand-off only removes privileged work; it grants no authority.
	static constexpr bool shouldGrantInstallAccess(bool installGrantsPrepared = false)
	{
		return !installGrantsPrepared;
	}
	static Result uninstall(const std::wstring& installDir,
		IRegistry& registry = systemRegistry());
	using DeviceUninstallErrorSink = std::function<void(const std::wstring&)>;
	// The default endpoint (render or capture) the device sweep hands to each
	// endpoint's load(). Empty means DeviceAPOInfo::getDefaultDevice, which
	// asks COM; tests pass a fixed answer so the sweep runs on a fake registry.
	using DefaultDeviceLookup = std::function<std::wstring(bool input)>;
	// Removes the APO from every endpoint, Voicemeeter strip and ASIO entry.
	// A failure on one item (an endpoint whose values cannot be read, an
	// enumeration that throws) is reported through errorSink and the sweep
	// goes on with the rest; the result is DeviceUninstallFailed then. It
	// never lets a RegistryError or DeviceException escape (audit #348
	// TD-02: one unreadable endpoint used to abort the whole uninstall hook
	// with AudioSrv still stopped).
	static Result uninstallAllDeviceApos(const DeviceUninstallErrorSink& errorSink,
		IRegistry& registry = systemRegistry(),
		const DefaultDeviceLookup& defaultDeviceLookup = {});

	// The registry role of install()/uninstall(), named and callable on its
	// own: writes (or cleans) the HKLM app vocabulary - InstallPath, the
	// ConfigPath/EnableTrace defaults that never overwrite user values, and
	// DisableProtectedAudioDG. configDir is created if missing (the F034
	// contract: Success must not point ConfigPath at a directory that does
	// not exist). cleanup deletes the app key only once it is empty.
	static Result writeAppInstallRegistry(const std::wstring& installDir,
		const std::wstring& configDir, IRegistry& registry = systemRegistry());
	static void cleanupAppRegistry(IRegistry& registry = systemRegistry());

	static bool stopAudioService();
	static bool startAudioService();

	// Grants Users and LOCAL SERVICE modify on a config
	// directory (recursive), so the user can edit configs and audiodg can
	// read them (and write APO trace logs). install() applies it to the
	// packaged config dir; the legacy migration applies it to the stable
	// config root it creates. Returns false when icacls reports a failure.
	static bool secureConfigDir(const std::wstring& configDir);

	// Registers (or unregisters) the EqualizerAPO COM in-proc server by calling
	// its DllRegisterServer / DllUnregisterServer export directly, instead of
	// spawning regsvr32.exe. Avoids the external process and returns the real
	// HRESULT (0 on success). The DLL is loaded only for the duration of the call.
	static int registerComServer(const std::wstring& dllPath, bool unregister);
};
