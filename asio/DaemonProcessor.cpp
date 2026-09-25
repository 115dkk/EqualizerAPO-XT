/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/DaemonProcessor.h"

#include <algorithm>
#include <cstring>

namespace eapo::asio
{
	namespace
	{
		using eapo::ipc::RingState;
		using eapo::ipc::RingWait;

		void copyMessage(char (&destination)[124], const std::string& source) noexcept
		{
			size_t i = 0;
			for (; i + 1 < sizeof(destination) && i < source.size(); i++)
				destination[i] = source[i];
			destination[i] = '\0';
		}

		inline unsigned slotOf(Direction direction) noexcept
		{
			return static_cast<unsigned>(direction);
		}
	}

	DaemonProcessor::DaemonProcessor(std::unique_ptr<IHostLink> link)
		: link_(std::move(link))
	{
	}

	DaemonProcessor::~DaemonProcessor()
	{
		teardown();
	}

	void DaemonProcessor::record(Direction direction) noexcept
	{
		const eapo::ipc::RingProducer::HandoffTiming timing = producer_->lastHandoff(direction);
		if (timing.dispatchUs > profile_.maxDispatchUs)
			profile_.maxDispatchUs = timing.dispatchUs;
		if (timing.serviceUs > profile_.maxServiceUs)
			profile_.maxServiceUs = timing.serviceUs;
		if (timing.returnUs > profile_.maxReturnUs)
			profile_.maxReturnUs = timing.returnUs;
		const uint32_t total = timing.dispatchUs + timing.serviceUs + timing.returnUs;
		const unsigned bucket = total < 100 ? 0 : total < 200 ? 1 : total < 500 ? 2 : total < 1000 ? 3 : total < 2000 ? 4 : 5;
		profile_.roundTripBuckets[bucket]++;
		profile_.completed++;
	}

	OpenReport DaemonProcessor::open(const StreamFormat& format, const StreamOptions& options)
	{
		OpenReport report;
		teardown();
		profile_ = HandoffProfile();
		if (!eapo::ipc::RingGeometry::validFormat(format))
		{
			report.status = OpenReport::Status::Rejected;
			copyMessage(report.message, "EQ APO XT cannot serve this channel count or buffer size");
			return report;
		}

		format_ = format;
		mode_ = options.mode;
		const double period = periodUs(format);
		deadlineUs_ = syncDeadlineUs(format, options);
		pipelineSpinUs_ = static_cast<uint32_t>(period * 0.1);
		if (pipelineSpinUs_ > 500)
			pipelineSpinUs_ = 500;
		// Gone after eight periods of lateness or 20 ms, whichever is longer,
		// counted in blocks.
		uint32_t hangBoundUs = static_cast<uint32_t>(period * 8.0);
		if (hangBoundUs < 20000)
			hangBoundUs = 20000;
		hangLateCount_ = period > 0.0 ? static_cast<uint32_t>(static_cast<double>(hangBoundUs) / period) : 8;
		if (period > 0.0 && period * hangLateCount_ < hangBoundUs)
			hangLateCount_++;

		for (unsigned slot = 0; slot < directionCount; slot++)
		{
			Lane& lane = lanes_[slot];
			const uint32_t channels = format.channels[slot];
			lane.samples = static_cast<size_t>(channels) * format.frames;
			lane.sequence = 0;
			lane.consecutiveLate = 0;
			lane.gone = false;
			lane.staging.assign(lane.samples, 0.0f);
			lane.previousInput.assign(mode_ == Mode::Pipelined ? lane.samples : 0, 0.0f);
			lane.planes.resize(channels);
			for (uint32_t c = 0; c < channels; c++)
				lane.planes[c] = lane.staging.data() + static_cast<size_t>(c) * format.frames;
		}

		std::string error;
		if (!link_->open(format, options, session_, error))
		{
			report.status = OpenReport::Status::Unavailable;
			copyMessage(report.message, error.empty() ? "EQ APO XT engine host could not be reached" : error);
			teardown();
			return report;
		}

		producer_ = std::make_unique<eapo::ipc::RingProducer>(session_.ringBase, format, GetCurrentProcessId(), session_.sync);
		const RingState state = producer_->waitReady(options.readyTimeoutMs);
		if (state != RingState::Ready)
		{
			if (state == RingState::Fault)
			{
				report.status = producer_->fault() == eapo::ipc::RingFault::LayoutMismatch
					? OpenReport::Status::Rejected : OpenReport::Status::Unavailable;
				copyMessage(report.message, producer_->fault() == eapo::ipc::RingFault::LayoutMismatch
					? "EQ APO XT engine host does not understand this wrapper's stream layout"
					: "EQ APO XT engine host could not load the configuration for this stream");
			}
			else
			{
				report.status = OpenReport::Status::Unavailable;
				copyMessage(report.message, "EQ APO XT engine host did not become ready in time");
			}
			producer_->close();
			teardown();
			return report;
		}

		open_ = true;
		report.status = OpenReport::Status::Ok;
		report.extraLatencyFrames = mode_ == Mode::Pipelined ? format.frames : 0;
		for (unsigned slot = 0; slot < directionCount; slot++)
			report.planes[slot] = format.channels[slot] > 0 ? lanes_[slot].planes.data() : nullptr;
		return report;
	}

	Outcome DaemonProcessor::process(Direction direction) noexcept
	{
		Lane& lane = lanes_[slotOf(direction)];
		if (!open_ || lane.samples == 0)
			return Outcome::Off;
		if (lane.gone)
			return Outcome::Gone;

		const uint32_t seq = lane.sequence + 1;
		if (!producer_->canPublish(direction, seq))
		{
			if (producer_->peerGone() || producer_->state() != RingState::Ready)
			{
				lane.gone = true;
				return Outcome::Gone;
			}
			if (mode_ == Mode::Pipelined && ++lane.consecutiveLate >= hangLateCount_)
			{
				lane.gone = true;
				return Outcome::Gone;
			}
			if (mode_ == Mode::Pipelined)
				holdTimeline(lane);
			return Outcome::Late;      // the host is two blocks behind; drop this one
		}
		lane.sequence = seq;
		std::memcpy(producer_->slot(direction, seq), lane.staging.data(), lane.samples * sizeof(float));
		producer_->publish(direction, seq);

		if (mode_ == Mode::Sync)
		{
			const RingWait waited = producer_->wait(direction, seq, deadlineUs_);
			if (waited == RingWait::Done)
			{
				record(direction);
				std::memcpy(lane.staging.data(), producer_->slot(direction, seq), lane.samples * sizeof(float));
				return Outcome::Processed;
			}
			if (waited == RingWait::Gone)
			{
				lane.gone = true;
				return Outcome::Gone;
			}
			return Outcome::Late;
		}

		// Pipelined: the block before this one is what goes out now.
		if (seq == 1)
		{
			std::memcpy(lane.previousInput.data(), lane.staging.data(), lane.samples * sizeof(float));
			std::memset(lane.staging.data(), 0, lane.samples * sizeof(float));
			return Outcome::Processed;
		}
		const RingWait waited = producer_->poll(direction, seq - 1, pipelineSpinUs_);
		if (waited == RingWait::Done)
		{
			lane.consecutiveLate = 0;
			record(direction);
			std::memcpy(lane.previousInput.data(), lane.staging.data(), lane.samples * sizeof(float));
			std::memcpy(lane.staging.data(), producer_->slot(direction, seq - 1), lane.samples * sizeof(float));
			return Outcome::Processed;
		}
		if (waited == RingWait::Gone)
		{
			lane.gone = true;
			return Outcome::Gone;
		}
		if (++lane.consecutiveLate >= hangLateCount_)
		{
			lane.gone = true;
			return Outcome::Gone;
		}
		holdTimeline(lane);
		return Outcome::Late;
	}

	void DaemonProcessor::holdTimeline(Lane& lane) noexcept
	{
		// The planes take the previous block's original audio and the
		// current block's becomes the previous one, in one pass and without
		// a third buffer.
		std::swap_ranges(lane.staging.begin(), lane.staging.end(), lane.previousInput.begin());
	}

	void DaemonProcessor::close(const StreamStats& stats) noexcept
	{
		lastStats_ = stats;
		teardown();
	}

	void DaemonProcessor::teardown() noexcept
	{
		if (producer_ != nullptr)
		{
			producer_->close();
			producer_.reset();
		}
		if (session_.ringBase != nullptr)
			link_->close(session_);
		session_ = HostSession();
		for (Lane& lane : lanes_)
		{
			lane.staging.clear();
			lane.previousInput.clear();
			lane.planes.clear();
			lane.samples = 0;
			lane.sequence = 0;
			lane.consecutiveLate = 0;
			lane.gone = false;
		}
		open_ = false;
	}
}
