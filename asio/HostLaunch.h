/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The process boundary of EqualizerAPOHost: its installed name, command-line
	grammar, defaults, and the one CreateProcessW policy used by every launcher.
*/

#pragma once

#include <cstdint>
#include <string>

namespace eapo::asio::HostLaunch
{
	inline constexpr wchar_t executableFileName[] = L"EqualizerAPOHost.exe";
	inline constexpr wchar_t endpointFlag[] = L"--endpoint";
	inline constexpr wchar_t lingerFlag[] = L"--linger";
	inline constexpr wchar_t residentFlag[] = L"--resident";
	inline constexpr uint32_t defaultLingerMs = 60000;

	struct Arguments
	{
		std::wstring endpoint;
		uint32_t lingerMs = defaultLingerMs;
		bool resident = false;
	};

	struct LaunchResult
	{
		uint32_t error = 0;
		uint32_t processId = 0;

		explicit operator bool() const noexcept {return error == 0;}
	};

	inline std::wstring executablePath(const std::wstring& directory)
	{
		return directory + L"\\" + executableFileName;
	}

	inline std::wstring streamCommandLine(const std::wstring& executable, const std::wstring& endpoint, uint32_t lingerMs)
	{
		return L"\"" + executable + L"\" " + endpointFlag + L" \"" + endpoint + L"\" " + lingerFlag + L" "
			+ std::to_wstring(lingerMs);
	}

	inline std::wstring residentCommandLine(const std::wstring& executable)
	{
		return L"\"" + executable + L"\" " + residentFlag;
	}

	Arguments parseCommandLine(int argc, wchar_t* const* argv);
	LaunchResult launch(const std::wstring& executable, std::wstring commandLine) noexcept;
}
