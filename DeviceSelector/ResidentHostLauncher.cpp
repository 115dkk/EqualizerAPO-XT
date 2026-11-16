/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "ResidentHostLauncher.h"

#include "asio/HostLaunch.h"
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
		const std::wstring exe = eapo::asio::HostLaunch::executablePath(directory);
		if (!pathutil::fileExists(exe))
			return L"Send keepalive: " + std::wstring(eapo::asio::HostLaunch::executableFileName) + L" is missing; host not started";
		winutil::UniqueHandle owner(OpenMutexW(SYNCHRONIZE, FALSE,
			eapo::asio::HostNames::owner(eapo::asio::HostNames::defaultEndpoint()).c_str()));
		if (owner)
			return L"Send keepalive: a host already serves this session and will pick up the receiver";

		// DeviceSelector requires elevation, so this host inherits it until logoff,
		// like the Voicemeeter client. The pipe's per-user SDDL still admits a
		// medium-integrity DAW; the Run entry starts the next host unelevated.
		const eapo::asio::HostLaunch::LaunchResult launched = eapo::asio::HostLaunch::launch(exe,
			eapo::asio::HostLaunch::residentCommandLine(exe));
		if (!launched)
			return L"Send keepalive: could not start host (error " + std::to_wstring(launched.error) + L")";
		return L"Send keepalive: started host (pid " + std::to_wstring(launched.processId) + L")";
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
