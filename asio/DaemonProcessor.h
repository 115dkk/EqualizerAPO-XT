/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The daemon adapter at the processor seam: the wrapper's planes are
	private staging buffers, and every process() copies them into the ring
	slot, publishes, waits, and copies the result back. Two shapes:

	  Sync       publish(seq), wait(seq, deadline). Done -> Processed;
	             the deadline passing -> Late (the wrapper writes the
	             original audio for that block, the host still processes it
	             in order); the host gone -> Gone for the rest of the stream.
	  Pipelined  publish(seq), then take seq - 1, which is normally complete
	             already. One block of extra latency, no per-block deadline;
	             a host that stops answering for eight periods is Gone.

	A slot the host may still be reading (two behind) is never overwritten:
	that block is dropped and reported Late without touching the ring.
*/

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "asio/HostLink.h"
#include "asio/StreamProcessor.h"
#include "runtime/ipc/StreamRing.h"

namespace eapo::asio
{
	class DaemonProcessor final : public IStreamProcessor
	{
	public:
		explicit DaemonProcessor(std::unique_ptr<IHostLink> link);
		~DaemonProcessor() override;

		DaemonProcessor(const DaemonProcessor&) = delete;
		DaemonProcessor& operator=(const DaemonProcessor&) = delete;

		OpenReport open(const StreamFormat& format, const StreamOptions& options) override;
		Outcome process(Direction direction) noexcept override;
		void close(const StreamStats& stats) noexcept override;

		// The deadline the last open() settled on, for the probe's report.
		uint32_t deadlineUs() const noexcept {return deadlineUs_;}
		const StreamStats& lastStats() const noexcept {return lastStats_;}

		// Where the handoff time went, over every completed block since
		// open(): the worst of each leg and a histogram of the whole round
		// trip. What the maintainer reads against the buffer period.
		struct HandoffProfile
		{
			uint32_t maxDispatchUs = 0;
			uint32_t maxServiceUs = 0;
			uint32_t maxReturnUs = 0;
			uint64_t roundTripBuckets[6] = {0, 0, 0, 0, 0, 0};   // <100, <200, <500, <1000, <2000, >=2000 us
			uint64_t completed = 0;
		};
		const HandoffProfile& profile() const noexcept {return profile_;}

	private:
		struct Lane
		{
			std::vector<float> staging;
			std::vector<float*> planes;
			uint32_t sequence = 0;      // last published
			uint32_t consecutiveLate = 0;
			size_t samples = 0;
			bool gone = false;
		};

		void teardown() noexcept;

		std::unique_ptr<IHostLink> link_;
		HostSession session_;
		std::unique_ptr<eapo::ipc::RingProducer> producer_;
		Lane lanes_[directionCount];
		StreamFormat format_;
		Mode mode_ = Mode::Sync;
		uint32_t deadlineUs_ = 0;
		uint32_t pipelineSpinUs_ = 0;
		uint32_t hangLateCount_ = 0;
		bool open_ = false;
		StreamStats lastStats_;
		HandoffProfile profile_;

		void record(Direction direction) noexcept;
	};
}
