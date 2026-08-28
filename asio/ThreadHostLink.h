/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The test-side host link: the same ring and the same serving loop as the
	real host, over a heap region and unnamed events, with the engine host
	running on a thread inside the calling process. It is how the daemon
	adapter's protocol and deadline logic are exercised on every PR without
	a second process, and how the probe measures the handoff cost without
	process-spawn noise. Not part of the wrapper DLL: it links the engine.
*/

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "asio/HostLink.h"

namespace eapo::asio
{
	class ThreadHostLink final : public IHostLink
	{
	public:
		// proAudio lifts the serving thread to the MMCSS Pro Audio class,
		// as the real host does; the tests leave it off.
		explicit ThreadHostLink(bool proAudio = false);
		~ThreadHostLink() override;

		ThreadHostLink(const ThreadHostLink&) = delete;
		ThreadHostLink& operator=(const ThreadHostLink&) = delete;

		bool open(const StreamFormat& format, const StreamOptions& options, HostSession& session, std::string& error) override;
		void close(HostSession& session) noexcept override;

		// Makes the serving thread leave without releasing what it holds,
		// the way a crashed host would. For the tests.
		void killHost() noexcept;

	private:
		void* region_ = nullptr;
		HANDLE events_[5] = {};
		HANDLE hostGone_ = nullptr;        // producer's peer: set when the thread leaves
		HANDLE producerGone_ = nullptr;    // consumer's peer: set by close()
		std::thread thread_;
		std::atomic<bool> kill_{false};
		bool proAudio_;
	};
}
