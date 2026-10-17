/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The named-pipe control server shared by EqualizerAPOHost and AsioTests. It
	owns only the pipe protocol and idle lifetime. Opening and serving a ring is
	injected as onOpen(HostOpenRequest) -> HostOpenReply.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "asio/HostProtocol.h"

namespace eapo::asio
{
	// A normal host may stay around for at most ten minutes after its last
	// stream. Permanent residency is an explicit --resident choice instead of a
	// linger value supplied by any same-user client.
	inline constexpr uint32_t maxHostLingerMs = 10u * 60u * 1000u;

	class HostActivity
	{
	public:
		HostActivity() noexcept;
		void started() noexcept;
		void finished() noexcept;
		int activeStreams() const noexcept;
		ULONGLONG idleSince() const noexcept;

	private:
		std::atomic<int> activeStreams_{0};
		std::atomic<ULONGLONG> idleSince_{0};
	};

	struct HostControlOptions
	{
		std::wstring endpoint;
		uint32_t lingerMs = 60000;
		bool resident = false;
		uint32_t transferTimeoutMs = 5000;
	};

	class HostControlServer
	{
	public:
		using OpenHandler = std::function<HostOpenReply(HostOpenRequest)>;

		HostControlServer(HostControlOptions options, HostActivity& activity, OpenHandler onOpen);
		~HostControlServer();

		HostControlServer(const HostControlServer&) = delete;
		HostControlServer& operator=(const HostControlServer&) = delete;

		int run() noexcept;
		void requestStop() noexcept;

		uint32_t lingerMs() const noexcept {return lingerMs_.load(std::memory_order_acquire);}
		uint64_t instanceHandoffs() const noexcept {return instanceHandoffs_.load(std::memory_order_acquire);}

	private:
		HostOpenReply handleConnection(HANDLE pipe) noexcept;

		HostControlOptions options_;
		HostActivity& activity_;
		OpenHandler onOpen_;
		std::atomic<uint32_t> lingerMs_{60000};
		std::atomic<bool> stopping_{false};
		std::atomic<uint64_t> instanceHandoffs_{0};
	};
}
