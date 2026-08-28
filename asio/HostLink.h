/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The port between the daemon adapter and whatever runs the engine host:
	a real process reached through the control pipe (Win32HostLink), or a
	thread inside the calling process for the tests and the probe
	(ThreadHostLink). Both hand back the same thing: a mapped ring region,
	the kernel objects the ring needs, and a handle that signals when the
	host is gone.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "asio/StreamProcessor.h"
#include "runtime/ipc/StreamRing.h"

namespace eapo::asio
{
	struct HostSession
	{
		void* ringBase = nullptr;
		size_t ringBytes = 0;
		eapo::ipc::RingSync sync;       // work/done/ready plus peer = the host
		uint32_t hostPid = 0;
	};

	class IHostLink
	{
	public:
		virtual ~IHostLink() = default;

		// Creates the region and its objects, reaches (or starts) the host and
		// asks it to serve `format` from `options.configPath`. On return the
		// host has been told; readiness is then waited for on the ring. False
		// with `error` filled when the host could not be reached at all.
		virtual bool open(const StreamFormat& format, const StreamOptions& options, HostSession& session, std::string& error) = 0;

		// Releases everything open() created. The ring must already be Closing.
		virtual void close(HostSession& session) noexcept = 0;
	};
}
