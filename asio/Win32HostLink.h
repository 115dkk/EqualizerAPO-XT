/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The production host link: the ring lives in a named file mapping, its
	events are named, and the engine host is a separate process reached
	through the control pipe. If no host answers on the endpoint the link
	starts EqualizerAPOHost.exe (beside the wrapper DLL unless the options
	name another path) and retries until the ready timeout. The host's
	process handle is the ring's peer, so a host that dies mid-stream turns
	the next wait into Gone.

	Before it sends anything the link checks that the process serving the
	pipe is that host executable, and the request and reply share the ready
	deadline instead of waiting without limit (audit #348 TD-46).
*/

#pragma once

#include <string>

#include "asio/HostLink.h"

namespace eapo::asio
{
	class Win32HostLink final : public IHostLink
	{
	public:
		Win32HostLink() = default;

		bool open(const StreamFormat& format, const StreamOptions& options, HostSession& session, std::string& error) override;
		void close(HostSession& session) noexcept override;

		// Where the wrapper DLL (or the probe) lives; the host exe is looked
		// for there when the options carry no path.
		static std::wstring moduleDirectory();

		// The engine host this link starts and expects to find serving the
		// pipe: the options' path, else EqualizerAPOHost.exe beside the module,
		// else the one in the module's parent folder. The last case is the
		// 32-bit wrapper, which ships alone in the x86 folder and talks to the
		// 64-bit host.
		static std::wstring hostExecutable(const StreamOptions& options, const std::wstring& moduleDirectory);

	private:
		struct Objects
		{
			HANDLE mapping = nullptr;
			HANDLE events[5] = {};
		};

		bool connectToHost(const std::wstring& endpoint, const StreamOptions& options, ULONGLONG deadline, HANDLE& pipe,
			std::string& error);
		static bool spawnHost(const std::wstring& endpoint, const StreamOptions& options, std::string& error);

		Objects objects_;
	};
}
