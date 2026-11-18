/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "services/windows/AudioEngineRestart.h"

#include "services/windows/AudioServiceNames.h"
#include "services/windows/WindowsService.h"

namespace audioengine
{
	bool restartAudioEngine(const RestartService& restart, const StartService& start,
		const std::function<void(const std::wstring&)>& reportError)
	{
		try
		{
			try
			{
				restart(audioEndpointBuilderServiceName, 30000);
				start(audioServiceName, 30000);
				return true;
			}
			catch (const WindowsServiceError& error)
			{
				reportError(error.getMessage());
			}
			catch (...)
			{
				reportError(L"Restarting the Windows audio engine failed");
			}
		}
		catch (...)
		{
			// Reporting must not turn a service failure into an uncaught exception
			// in the one-shot elevated Editor process.
		}
		return false;
	}

	bool restartAudioEngine(const std::function<void(const std::wstring&)>& reportError)
	{
		try
		{
			return restartAudioEngine(WindowsServiceControl::restart, WindowsServiceControl::start, reportError);
		}
		catch (...)
		{
			// Even constructing the injected std::functions can exhaust memory.
			return false;
		}
	}

	RestartRequest requestRestart(const std::wstring& exePath, const ElevatedRun& run,
		const WaitForExit& wait, unsigned long timeoutMs)
	{
		try
		{
			auto launch = run(exePath, kRestartArgument);
			if (!launch.started)
				return launch.error == ERROR_CANCELLED ? RestartRequest::Declined : RestartRequest::Failed;
			if (!launch.process)
				return RestartRequest::Failed;
			const auto exitCode = wait(launch.process.get(), timeoutMs);
			return exitCode && *exitCode == 0 ? RestartRequest::Restarted : RestartRequest::Failed;
		}
		catch (...)
		{
			return RestartRequest::Failed;
		}
	}

	RestartRequest requestRestart(const std::wstring& exePath, const ElevatedRun& run, unsigned long timeoutMs)
	{
		return requestRestart(exePath, run, [](HANDLE process, unsigned long timeout) -> std::optional<unsigned long> {
			if (WaitForSingleObject(process, timeout) != WAIT_OBJECT_0)
				return std::nullopt;
			DWORD exitCode = 1;
			if (!GetExitCodeProcess(process, &exitCode))
				return std::nullopt;
			return exitCode;
		}, timeoutMs);
	}
}
