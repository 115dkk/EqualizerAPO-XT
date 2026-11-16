/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	EqualizerAPOHost.exe: the engine host for ASIO streams
	(docs/architecture/asio-host-study.md, sections 9-10). One per session
	endpoint, started on demand by the first wrapper that needs it. The shared
	HostControlServer owns the named-pipe protocol and idle lifetime, and
	StreamHost maps each requested ring and starts its engine thread. This file
	parses the process arguments and assembles those parts.
*/

#include <thread>

#include "EqualizerAPOHost/Keepalive.h"
#include "asio/HostControlServer.h"
#include "asio/HostLaunch.h"
#include "asio/StreamHost.h"
#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"
#include "vst/PluginResidency.h"

// After windows.h (through the ring header): shellapi.h needs its types.
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	PluginResidency::keepLoadedForProcessLifetime(true);

	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	const eapo::asio::HostLaunch::Arguments arguments = eapo::asio::HostLaunch::parseCommandLine(argc, argv);
	if (argv != nullptr)
		LocalFree(argv);
	Logging::useUserFile(L"EqualizerAPOHost.log", false, false, false);

	winutil::UniqueHandle owner(CreateMutexW(nullptr, TRUE, eapo::asio::HostNames::owner(arguments.endpoint).c_str()));
	if (!owner)
		return 2;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		return 0;

	eapo::asio::HostActivity activity;
	eapo::asio::StreamHost streams(activity, systemRegistry());
	eapo::asio::HostControlOptions controlOptions;
	controlOptions.endpoint = arguments.endpoint;
	controlOptions.lingerMs = arguments.lingerMs;
	controlOptions.resident = arguments.resident;
	eapo::asio::HostControlServer server(controlOptions, activity,
		// cppcheck-suppress throwInEntryPoint ; HostControlServer calls this handler inside catch (...) and answers the client with a failure reply
		[&streams](eapo::asio::HostOpenRequest request) {return streams.open(request);});

	int result;
	{
		// Every host keeps receiving endpoints awake, even when started by a DAW.
		// Held for its lifetime only; scope exit stops and joins the supervisor.
		// cppcheck-suppress unreadVariable
		std::jthread keepalive = startKeepalive(activity);
		result = server.run();
	}
	streams.stop();
	ReleaseMutex(owner.get());
	return result;
}
