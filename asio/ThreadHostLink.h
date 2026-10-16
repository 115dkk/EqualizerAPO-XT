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
#include "asio/HostProtocol.h"

namespace eapo::asio
{
	// Test-side controls are kept separately from the link so a test that gives
	// the link to a unique_ptr never keeps a raw alias to it.
	class ThreadHostControl
	{
	public:
		void killHost() noexcept;
		void holdHost(bool held) noexcept;

	private:
		friend class ThreadHostLink;
		std::atomic<bool> kill_{false};
		std::atomic<bool> held_{false};
		std::atomic<HANDLE> thread_{nullptr};
	};

	class ThreadHostLink final : public IHostLink
	{
	public:
		// proAudio lifts the serving thread to the MMCSS Pro Audio class,
		// as the real host does; traceSlowUs is a probe-only diagnostic.
		explicit ThreadHostLink(bool proAudio = false, uint32_t traceSlowUs = 0,
			std::shared_ptr<ThreadHostControl> control = {});
		~ThreadHostLink() override;

		ThreadHostLink(const ThreadHostLink&) = delete;
		ThreadHostLink& operator=(const ThreadHostLink&) = delete;

		std::unique_ptr<eapo::ipc::RingProducer> open(const StreamFormat& format, const StreamOptions& options,
			OpenReport& report) override;
		void close(std::unique_ptr<eapo::ipc::RingProducer> producer) noexcept override;

	private:
		void* region_ = nullptr;
		winutil::UniqueHandle events_[RingEvents::count];
		winutil::UniqueHandle hostGone_;        // producer's peer: set when the thread leaves
		winutil::UniqueHandle producerGone_;    // consumer's peer: set by close()
		std::thread thread_;
		std::shared_ptr<ThreadHostControl> control_;
		HostSession session_;
		bool proAudio_;
		uint32_t traceSlowUs_;
	};
}
