/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/HostLaunch.h"

#include <cwchar>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "asio/HostProtocol.h"
#include "platform/windows/Win32Resource.h"
#include "services/shell/UnelevatedLaunch.h"

namespace eapo::asio::HostLaunch
{
	Arguments parseCommandLine(int argc, wchar_t* const* argv)
	{
		Arguments arguments;
		for (int i = 1; i < argc; i++)
		{
			const std::wstring key = argv[i];
			if (key == endpointFlag && i + 1 < argc)
				arguments.endpoint = argv[++i];
			else if (key == lingerFlag && i + 1 < argc)
				arguments.lingerMs = static_cast<uint32_t>(std::wcstoul(argv[++i], nullptr, 10));
			else if (key == residentFlag)
				arguments.resident = true;
		}
		if (arguments.endpoint.empty())
			arguments.endpoint = HostNames::defaultEndpoint();
		return arguments;
	}

	// commandLine is taken by value: CreateProcessW may write into the buffer.
	LaunchResult launch(const std::wstring& executable, std::wstring commandLine, bool unelevated) noexcept
	{
		if (unelevated)
		{
			const auto result = winutil::launchUnelevated(executable, std::move(commandLine), CREATE_NO_WINDOW);
			return {result.error, result.processId};
		}
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		// A caller's environment and working directory are not the host's
		// business; only the executable's folder matters for its DLLs.
		const std::wstring workingDirectory = executable.substr(0, executable.find_last_of(L"\\/"));
		if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
			workingDirectory.c_str(), &startup, &process))
		{
			LaunchResult result;
			result.error = GetLastError();
			return result;
		}
		winutil::UniqueHandle threadHandle(process.hThread);
		winutil::UniqueHandle processHandle(process.hProcess);
		LaunchResult result;
		result.processId = process.dwProcessId;
		return result;
	}
}
