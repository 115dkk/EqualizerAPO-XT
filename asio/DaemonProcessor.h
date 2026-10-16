/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The daemon adapter at the processor seam. planes() points the wrapper's
	conversion directly at the ring slot for the block, so process() publishes
	and receives without a staging-to-ring or ring-to-staging copy.

	Sync publishes and waits to the deadline. A timeout is LateDropped because
	the consumer may still own the slot. Pipelined publishes this block and
	returns the preceding slot. A late pipelined result is LateHeld and returns
	the preceding block's original audio, preserving the reported one-block
	latency. The one copy retained in that mode saves the current original
	before the host processes its ring slot in place.
*/

#pragma once

#include <memory>
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
		float** planes(Direction direction) noexcept override;
		Outcome process(Direction direction) noexcept override;
		void close() noexcept override;
		const HandoffProfile* profile() const noexcept override {return &profile_;}

		// The deadline the last open() settled on, for the probe's report.
		uint32_t deadlineUs() const noexcept {return deadlineUs_;}

	private:
		struct Lane
		{
			// Scratch receives a block when its ring slot is still busy. In
			// pipelined mode scratch and heldInput swap ownership to hold the
			// preceding original without another copy.
			std::vector<float> scratch;
			std::vector<float> heldInput;
			std::vector<float> silence;
			std::vector<float*> planes;
			uint32_t sequence = 0;          // last published
			uint32_t preparedSequence = 0;  // the sequence planes() selected
			uint32_t consecutiveLate = 0;
			size_t samples = 0;
			bool publishable = false;
			bool prepared = false;
			bool gone = false;
		};

		void teardown() noexcept;
		void pointPlanes(Lane& lane, float* samples) noexcept;
		void holdTimeline(Lane& lane) noexcept;
		void record(Direction direction) noexcept;

		std::unique_ptr<IHostLink> link_;
		std::unique_ptr<eapo::ipc::RingProducer> producer_;
		Lane lanes_[directionCount];
		StreamFormat format_;
		Mode mode_ = Mode::Sync;
		uint32_t deadlineUs_ = 0;
		uint32_t pipelineSpinUs_ = 0;
		uint32_t hangLateCount_ = 0;
		bool open_ = false;
		HandoffProfile profile_;
	};
}
