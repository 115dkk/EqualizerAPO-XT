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

	static Result install(const std::wstring& installDir);
	static Result uninstall(const std::wstring& installDir);
	using DeviceUninstallErrorSink = std::function<void(const std::wstring&)>;
	static Result uninstallAllDeviceApos(const DeviceUninstallErrorSink& errorSink);

	static bool stopAudioService();
	static bool startAudioService();

	// Grants Users full control and LOCAL SERVICE modify on a config
	// directory (recursive), so the user can edit configs and audiodg can
	// read them (and write APO trace logs). install() applies it to the
	// packaged config dir; the legacy migration applies it to the stable
	// config root it creates. Returns false when icacls reports a failure.
	static bool secureConfigDir(const std::wstring& configDir);

	static int waitForProcess(const std::wstring& executable, const std::wstring& arguments, unsigned timeoutMs = 30000);

	// Registers (or unregisters) the EqualizerAPO COM in-proc server by calling
	// its DllRegisterServer / DllUnregisterServer export directly, instead of
	// spawning regsvr32.exe. Avoids the external process and returns the real
	// HRESULT (0 on success). The DLL is loaded only for the duration of the call.
	static int registerComServer(const std::wstring& dllPath, bool unregister);

	static bool createStartMenuShortcuts(const std::wstring& installDir);
	static bool removeStartMenuShortcuts();
};
