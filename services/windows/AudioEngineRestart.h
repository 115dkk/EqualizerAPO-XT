/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "platform/windows/ElevatedLaunch.h"

namespace audioengine
{
	inline constexpr wchar_t kRestartArgument[] = L"--eapo-restart-audio-engine";
	using RestartService = std::function<void(const std::wstring& serviceName, unsigned timeoutMs)>;
	using StartService = std::function<void(const std::wstring& serviceName, unsigned timeoutMs)>;

	// Elevated only. Recycle audiodg through AudioEndpointBuilder, including its
	// running dependents, then ensure AudioSrv runs even if it was stopped before.
	bool restartAudioEngine(const RestartService& restart, const StartService& start,
		const std::function<void(const std::wstring&)>& reportError);
	bool restartAudioEngine(const std::function<void(const std::wstring&)>& reportError);

	enum class RestartRequest { Restarted, Declined, Failed };
	using ElevatedRun = std::function<winutil::ElevatedLaunch(const std::wstring& exe, const std::wstring& args)>;
	// nullopt means timeout, failed wait, or failed exit-code query. The caller
	// retains ownership of the process handle. This seam never needs real UAC.
	using WaitForExit = std::function<std::optional<unsigned long>(HANDLE process, unsigned long timeoutMs)>;
	RestartRequest requestRestart(const std::wstring& exePath, const ElevatedRun& run, unsigned long timeoutMs = 60000);
	RestartRequest requestRestart(const std::wstring& exePath, const ElevatedRun& run,
		const WaitForExit& wait, unsigned long timeoutMs = 60000);
}
