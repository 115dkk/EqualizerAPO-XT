/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/DaemonProcessor.h"

#include <cstring>
#include <utility>

namespace eapo::asio
{
	using eapo::ipc::RingState;
	using eapo::ipc::RingWait;

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
			copyErrorMessage(report.message, "EQ APO XT cannot serve this channel count or buffer size");
			return report;
		}

		format_ = format;
		mode_ = options.mode;
		const double period = periodUs(format);
		deadlineUs_ = syncDeadlineUs(format, options);
		profile_.deadlineUs = deadlineUs_;
		pipelineSpinUs_ = static_cast<uint32_t>(period * 0.1);
		if (pipelineSpinUs_ > 500)
			pipelineSpinUs_ = 500;
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
			lane.preparedSequence = 0;
			lane.consecutiveLate = 0;
			lane.publishable = false;
			lane.prepared = false;
			lane.gone = false;
			lane.scratch.assign(lane.samples, 0.0f);
			lane.heldInput.assign(mode_ == Mode::Pipelined ? lane.samples : 0, 0.0f);
			lane.silence.assign(mode_ == Mode::Pipelined ? lane.samples : 0, 0.0f);
			lane.planes.resize(channels);
			pointPlanes(lane, lane.scratch.data());
		}

		producer_ = link_->open(format, options, report);
		if (producer_ == nullptr)
		{
			if (report.message[0] == '\0')
				copyErrorMessage(report.message, "EQ APO XT engine host could not be reached");
			teardown();
			return report;
		}

		open_ = true;
		report.status = OpenReport::Status::Ok;
		report.extraLatencyFrames = mode_ == Mode::Pipelined ? format.frames : 0;
		return report;
	}

	void DaemonProcessor::pointPlanes(Lane& lane, float* samples) noexcept
	{
		for (size_t c = 0; c < lane.planes.size(); c++)
			lane.planes[c] = samples + c * format_.frames;
	}

	float** DaemonProcessor::planes(Direction direction) noexcept
	{
		Lane& lane = lanes_[slotOf(direction)];
		if (!open_ || lane.samples == 0)
			return nullptr;
		lane.preparedSequence = lane.sequence + 1;
		lane.publishable = !lane.gone && producer_->canPublish(direction, lane.preparedSequence);
		lane.prepared = true;
		float* samples = lane.publishable ? producer_->slot(direction, lane.preparedSequence) : lane.scratch.data();
		pointPlanes(lane, samples);
		return lane.planes.data();
	}

	Outcome DaemonProcessor::process(Direction direction) noexcept
	{
		Lane& lane = lanes_[slotOf(direction)];
		if (!open_ || lane.samples == 0 || !lane.prepared)
			return Outcome::Off;
		lane.prepared = false;
		if (lane.gone)
			return Outcome::Gone;

		const uint32_t seq = lane.preparedSequence;
		if (!lane.publishable)
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
			{
				holdTimeline(lane);
				return Outcome::LateHeld;
			}
			return Outcome::LateDropped;
		}

		lane.sequence = seq;
		if (mode_ == Mode::Pipelined)
		{
			// Preserve this block's original before publish wakes the consumer,
			// which processes the ring slot in place.
			std::memcpy(lane.scratch.data(), producer_->slot(direction, seq), lane.samples * sizeof(float));
			std::swap(lane.scratch, lane.heldInput);
		}
		producer_->publish(direction, seq);

		if (mode_ == Mode::Sync)
		{
			const RingWait waited = producer_->wait(direction, seq, deadlineUs_);
			if (waited == RingWait::Done)
			{
				record(direction);
				return Outcome::Processed;
			}
			if (waited == RingWait::Gone)
			{
				lane.gone = true;
				return Outcome::Gone;
			}
			return Outcome::LateDropped;
		}

		if (seq == 1)
		{
			pointPlanes(lane, lane.silence.data());
			return Outcome::Processed;
		}
		const RingWait waited = producer_->poll(direction, seq - 1, pipelineSpinUs_);
		if (waited == RingWait::Done)
		{
			lane.consecutiveLate = 0;
			record(direction);
			pointPlanes(lane, producer_->slot(direction, seq - 1));
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
		pointPlanes(lane, lane.scratch.data());
		return Outcome::LateHeld;
	}

	void DaemonProcessor::holdTimeline(Lane& lane) noexcept
	{
		std::swap(lane.scratch, lane.heldInput);
		pointPlanes(lane, lane.scratch.data());
	}

	void DaemonProcessor::close() noexcept
	{
		teardown();
	}

	void DaemonProcessor::teardown() noexcept
	{
		link_->close(std::move(producer_));
		for (Lane& lane : lanes_)
		{
			lane.scratch.clear();
			lane.heldInput.clear();
			lane.silence.clear();
			lane.planes.clear();
			lane.samples = 0;
			lane.sequence = 0;
			lane.preparedSequence = 0;
			lane.consecutiveLate = 0;
			lane.publishable = false;
			lane.prepared = false;
			lane.gone = false;
		}
		open_ = false;
	}
}
