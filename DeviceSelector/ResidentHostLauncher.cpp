/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "ResidentHostLauncher.h"

#include "asio/HostProtocol.h"
#include "devices/AsioEntryWriter.h"
#include "devices/ReceiverEndpoints.h"
#include "platform/windows/WindowsPath.h"
#include "services/registry/WindowsRegistry.h"

namespace ResidentHostLauncher
{
std::wstring ensureStarted(const IRegistry& registry)
{
	try
	{
		if (receivingEndpoints(registry).empty())
			return L"Send keepalive: no playback endpoint receives Send audio";
		const std::wstring directory = AsioEntryWriter::installDirectory(registry, AsioEntryWriter::DirectoryRequirement::Optional);
		if (directory.empty())
			return L"Send keepalive: InstallPath is missing; host not started";
		const std::wstring exe = directory + L"\\EqualizerAPOHost.exe";
		if (!pathutil::fileExists(exe))
			return L"Send keepalive: EqualizerAPOHost.exe is missing; host not started";
		winutil::UniqueHandle owner(OpenMutexW(SYNCHRONIZE, FALSE,
			eapo::asio::HostNames::owner(eapo::asio::HostNames::defaultEndpoint()).c_str()));
		if (owner)
			return L"Send keepalive: a host already serves this session and will pick up the receiver";

		std::wstring commandLine = L"\"" + exe + L"\" --resident";
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		const std::wstring workingDirectory = exe.substr(0, exe.find_last_of(L"\\/"));
		// DeviceSelector requires elevation, so this host inherits it until logoff,
		// like the Voicemeeter client. The pipe's per-user SDDL still admits a
		// medium-integrity DAW; the Run entry starts the next host unelevated.
		if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
			workingDirectory.c_str(), &startup, &process))
		{
			const DWORD error = GetLastError();
			return L"Send keepalive: could not start host (error " + std::to_wstring(error) + L")";
		}
		winutil::UniqueHandle threadHandle(process.hThread);
		winutil::UniqueHandle processHandle(process.hProcess);
		return L"Send keepalive: started host (pid " + std::to_wstring(process.dwProcessId) + L")";
	}
	catch (...)
	{
		// A failed check or launch must not unwind a device change that has
		// already been committed.
		return L"Send keepalive: could not check or start host";
	}
}

std::wstring ensureStarted()
{
	return ensureStarted(systemRegistry());
}
}
