/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "runtime/ipc/StreamRing.h"

#include <cstring>

namespace eapo::ipc
{
	namespace
	{
		using eapo::asio::Direction;
		using eapo::asio::directionCount;

		inline unsigned laneOf(Direction direction) noexcept
		{
			return static_cast<unsigned>(direction);
		}

		inline uint64_t roundUp(uint64_t value, uint64_t alignment) noexcept
		{
			return (value + alignment - 1) / alignment * alignment;
		}

		// Sequence comparison that survives wrap-around (750 blocks/s wraps
		// a 32-bit counter after 66 days; a stream may run that long).
		inline bool sequenceAtLeast(uint32_t value, uint32_t reference) noexcept
		{
			return static_cast<int32_t>(value - reference) >= 0;
		}

		inline uint64_t tickNow() noexcept
		{
			LARGE_INTEGER counter;
			QueryPerformanceCounter(&counter);
			return static_cast<uint64_t>(counter.QuadPart);
		}

		inline uint32_t readState(const RingHeader* header) noexcept
		{
			return static_cast<uint32_t>(ReadAcquire(&header->state));
		}
	}

	namespace RingGeometry
	{
		bool validFormat(const eapo::asio::StreamFormat& format) noexcept
		{
			return format.frames >= 1 && format.frames <= maxRingFrames
				&& format.channels[0] <= maxRingChannels && format.channels[1] <= maxRingChannels
				&& (format.channels[0] != 0 || format.channels[1] != 0);
		}

		uint32_t slotBytes(const eapo::asio::StreamFormat& format, Direction direction) noexcept
		{
			const uint64_t samples = static_cast<uint64_t>(format.channelCount(direction)) * format.frames;
			const uint64_t bytes = samples == 0 ? 0 : roundUp(samples * sizeof(float), ringAlignment);
			// validFormat() bounds one slot to 16 MiB, so producer and consumer
			// callers may cast this checked geometry to the fixed-width wire type.
			return static_cast<uint32_t>(bytes);
		}

		uint32_t totalBytes(const eapo::asio::StreamFormat& format) noexcept
		{
			uint64_t total = ringHeaderBytes;
			for (unsigned lane = 0; lane < directionCount; lane++)
				total += 2ull * slotBytes(format, static_cast<Direction>(lane));
			// validFormat() keeps both lanes and the header below 128 MiB.
			return static_cast<uint32_t>(total);
		}
	}

	// ---- producer ----

	RingProducer::RingProducer(void* base, const eapo::asio::StreamFormat& format, uint32_t producerPid, const RingSync& sync) noexcept
		: header_(static_cast<RingHeader*>(base)), sync_(sync)
	{
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		ticksPerMicro_ = frequency.QuadPart > 0 ? static_cast<double>(frequency.QuadPart) / 1000000.0 : 0.0;

		std::memset(header_, 0, sizeof(RingHeader));
		header_->magic = ringMagic;
		header_->layoutVersion = ringLayoutVersion;
		header_->totalBytes = RingGeometry::totalBytes(format);
		header_->format = format;
		header_->producerPid = producerPid;
		uint32_t offset = static_cast<uint32_t>(ringHeaderBytes);
		for (unsigned lane = 0; lane < directionCount; lane++)
		{
			RingLane& entry = header_->lanes[lane];
			entry.sequence = 0;
			entry.completed = 0;
			entry.slotBytes = RingGeometry::slotBytes(format, static_cast<Direction>(lane));
			for (unsigned slot = 0; slot < 2; slot++)
			{
				entry.slotOffset[slot] = offset;
				offset += entry.slotBytes;
			}
		}
		header_->producerCpu = -1;
		WriteRelease(&header_->faultCode, static_cast<LONG>(RingFault::None));
		WriteRelease(&header_->state, static_cast<LONG>(RingState::Announced));
	}

	RingState RingProducer::waitReady(uint32_t timeoutMs) noexcept
	{
		HANDLE handles[2] = {sync_.ready, sync_.peer};
		const DWORD count = sync_.peer != nullptr ? 2 : 1;
		const uint64_t deadline = tickNow() + static_cast<uint64_t>(timeoutMs * 1000.0 * ticksPerMicro_);
		for (;;)
		{
			const RingState current = static_cast<RingState>(readState(header_));
			if (current == RingState::Ready || current == RingState::Fault || current == RingState::Closing)
				return current;
			const uint64_t now = tickNow();
			if (now >= deadline)
				return current;
			const DWORD remaining = static_cast<DWORD>(static_cast<double>(deadline - now) / (ticksPerMicro_ * 1000.0)) + 1;
			const DWORD result = WaitForMultipleObjects(count, handles, FALSE, remaining);
			if (result == WAIT_OBJECT_0 + 1)
				return static_cast<RingState>(readState(header_));   // the peer is gone; report what it left
			if (result == WAIT_FAILED)
				return static_cast<RingState>(readState(header_));
		}
	}

	RingFault RingProducer::fault() const noexcept
	{
		return static_cast<RingFault>(ReadAcquire(&header_->faultCode));
	}

	uint32_t RingProducer::consumerPid() const noexcept
	{
		return header_->consumerPid;
	}

	bool RingProducer::canPublish(Direction direction, uint32_t seq) const noexcept
	{
		const RingLane& lane = header_->lanes[laneOf(direction)];
		const uint32_t done = static_cast<uint32_t>(ReadAcquire(&lane.completed));
		// The slot seq & 1 was last used by seq - 2; it is free once that one
		// completed. seq 1 and 2 never wait.
		return seq <= 2 || sequenceAtLeast(done, seq - 2);
	}

	float* RingProducer::slot(Direction direction, uint32_t seq) noexcept
	{
		const RingLane& lane = header_->lanes[laneOf(direction)];
		return reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(header_) + lane.slotOffset[seq & 1]);
	}

	void RingProducer::publish(Direction direction, uint32_t seq) noexcept
	{
		RingLane& lane = header_->lanes[laneOf(direction)];
		header_->publishTick[laneOf(direction)] = static_cast<LONGLONG>(tickNow());
		header_->producerCpu = static_cast<LONG>(GetCurrentProcessorNumber());
		WriteRelease(&lane.sequence, static_cast<LONG>(seq));
		if (sync_.work[laneOf(direction)] != nullptr)
			SetEvent(sync_.work[laneOf(direction)]);
	}

	bool RingProducer::completed(Direction direction, uint32_t seq) const noexcept
	{
		const RingLane& lane = header_->lanes[laneOf(direction)];
		return sequenceAtLeast(static_cast<uint32_t>(ReadAcquire(&lane.completed)), seq);
	}

	bool RingProducer::peerGone() const noexcept
	{
		return sync_.peer != nullptr && WaitForSingleObject(sync_.peer, 0) != WAIT_TIMEOUT;
	}

	RingWait RingProducer::poll(Direction direction, uint32_t seq, uint32_t budgetUs) noexcept
	{
		const uint64_t deadline = tickNow() + static_cast<uint64_t>(budgetUs * ticksPerMicro_);
		for (;;)
		{
			if (completed(direction, seq))
				return RingWait::Done;
			if (readState(header_) != static_cast<uint32_t>(RingState::Ready) || peerGone())
				return RingWait::Gone;
			if (tickNow() >= deadline)
				return RingWait::Late;
			YieldProcessor();
		}
	}

	RingWait RingProducer::wait(Direction direction, uint32_t seq, uint32_t budgetUs) noexcept
	{
		const unsigned index = laneOf(direction);
		const uint64_t start = tickNow();
		const uint64_t deadline = start + static_cast<uint64_t>(budgetUs * ticksPerMicro_);
		HANDLE handles[2] = {sync_.done[index], sync_.peer};
		const DWORD count = sync_.peer != nullptr ? 2 : 1;
		for (;;)
		{
			if (completed(direction, seq))
				return RingWait::Done;
			const uint32_t current = readState(header_);
			if (current != static_cast<uint32_t>(RingState::Ready))
				return RingWait::Gone;
			const uint64_t now = tickNow();
			if (now >= deadline)
				return RingWait::Late;
			// Sub-millisecond budgets spin: the kernel wait rounds up to a
			// millisecond, which at 64 frames is most of the period. The spin
			// pauses, it does not yield: yielding handed this (the DAW's)
			// thread's core away for milliseconds at a time, which is an
			// overrun in the DAW, not a late block. The consumer keeps off
			// this core instead (producerCpu in the header).
			const uint64_t remainingTicks = deadline - now;
			const double remainingUs = static_cast<double>(remainingTicks) / ticksPerMicro_;
			if (remainingUs < 1000.0)
			{
				YieldProcessor();
				continue;
			}
			const DWORD result = WaitForMultipleObjects(count, handles, FALSE, static_cast<DWORD>(remainingUs / 1000.0));
			if (result == WAIT_OBJECT_0 + 1)
				return completed(direction, seq) ? RingWait::Done : RingWait::Gone;
			if (result == WAIT_FAILED)
				return RingWait::Gone;
		}
	}

	RingProducer::HandoffTiming RingProducer::lastHandoff(Direction direction) const noexcept
	{
		HandoffTiming timing;
		const unsigned index = laneOf(direction);
		const LONGLONG published = header_->publishTick[index];
		const LONGLONG acquired = header_->acquireTick[index];
		const LONGLONG completed = header_->completeTick[index];
		const LONGLONG now = static_cast<LONGLONG>(tickNow());
		if (ticksPerMicro_ <= 0.0 || acquired < published || completed < acquired)
			return timing;
		timing.dispatchUs = static_cast<uint32_t>(static_cast<double>(acquired - published) / ticksPerMicro_);
		timing.serviceUs = static_cast<uint32_t>(static_cast<double>(completed - acquired) / ticksPerMicro_);
		timing.returnUs = now > completed ? static_cast<uint32_t>(static_cast<double>(now - completed) / ticksPerMicro_) : 0;
		return timing;
	}

	void RingProducer::close() noexcept
	{
		WriteRelease(&header_->state, static_cast<LONG>(RingState::Closing));
		for (unsigned lane = 0; lane < directionCount; lane++)
		{
			if (sync_.work[lane] != nullptr)
				SetEvent(sync_.work[lane]);
		}
	}

	RingState RingProducer::state() const noexcept
	{
		return static_cast<RingState>(readState(header_));
	}

	// ---- consumer ----

	RingConsumer::RingConsumer(void* base, size_t bytes, const RingSync& sync) noexcept
		: header_(static_cast<RingHeader*>(base)), sync_(sync)
	{
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		ticksPerMicro_ = frequency.QuadPart > 0 ? static_cast<double>(frequency.QuadPart) / 1000000.0 : 0.0;
		if (header_ == nullptr || bytes < ringHeaderBytes)
			return;
		if (header_->magic != ringMagic || header_->layoutVersion != ringLayoutVersion)
			return;
		if (!RingGeometry::validFormat(header_->format))
			return;
		const uint64_t expectedBytes = RingGeometry::totalBytes(header_->format);
		if (expectedBytes != header_->totalBytes || expectedBytes > bytes)
			return;
		for (unsigned lane = 0; lane < directionCount; lane++)
		{
			const RingLane& entry = header_->lanes[lane];
			for (unsigned slot = 0; slot < 2; slot++)
			{
				if (static_cast<size_t>(entry.slotOffset[slot]) + entry.slotBytes > header_->totalBytes)
					return;
			}
		}
		valid_ = true;
	}

	void RingConsumer::setConsumerPid(uint32_t pid) noexcept
	{
		header_->consumerPid = pid;
	}

	void RingConsumer::setState(RingState state, RingFault fault) noexcept
	{
		WriteRelease(&header_->faultCode, static_cast<LONG>(fault));
		WriteRelease(&header_->state, static_cast<LONG>(state));
		if ((state == RingState::Ready || state == RingState::Fault) && sync_.ready != nullptr)
			SetEvent(sync_.ready);
		if (state == RingState::Fault)
		{
			for (unsigned lane = 0; lane < directionCount; lane++)
			{
				if (sync_.done[lane] != nullptr)
					SetEvent(sync_.done[lane]);
			}
		}
	}

	RingState RingConsumer::state() const noexcept
	{
		return static_cast<RingState>(readState(header_));
	}

	bool RingConsumer::pending(Direction direction, Acquired& out) noexcept
	{
		RingLane& lane = header_->lanes[laneOf(direction)];
		const uint32_t published = static_cast<uint32_t>(ReadAcquire(&lane.sequence));
		const uint32_t done = static_cast<uint32_t>(ReadAcquire(&lane.completed));
		if (published == done || !sequenceAtLeast(published, done + 1))
			return false;
		const uint32_t next = done + 1;
		out.direction = direction;
		out.sequence = next;
		out.slot = reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(header_) + lane.slotOffset[next & 1]);
		header_->acquireTick[laneOf(direction)] = static_cast<LONGLONG>(tickNow());
		return true;
	}

	bool RingConsumer::acquire(Acquired& out, uint32_t timeoutMs, uint32_t spinUs) noexcept
	{
		HANDLE handles[3] = {sync_.work[0], sync_.work[1], sync_.peer};
		const DWORD count = sync_.peer != nullptr ? 3 : 2;
		for (;;)
		{
			if (readState(header_) == static_cast<uint32_t>(RingState::Closing))
				return false;
			if (pending(Direction::Output, out) || pending(Direction::Input, out))
				return true;
			if (peerGone_)
				return false;
			if (spinUs != 0)
			{
				const uint64_t deadline = tickNow() + static_cast<uint64_t>(spinUs * ticksPerMicro_);
				while (tickNow() < deadline)
				{
					if (pending(Direction::Output, out) || pending(Direction::Input, out))
						return true;
					if (readState(header_) == static_cast<uint32_t>(RingState::Closing))
						return false;
					YieldProcessor();
				}
				// Events set while spinning stay set (auto-reset, unconsumed),
				// so the kernel wait below returns at once in that case.
			}
			const DWORD result = WaitForMultipleObjects(count, handles, FALSE, timeoutMs);
			if (result == WAIT_OBJECT_0 + 2)
			{
				peerGone_ = true;
				// Drain what was published before the peer died, then report.
				if (pending(Direction::Output, out) || pending(Direction::Input, out))
					return true;
				return false;
			}
			if (result == WAIT_TIMEOUT || result == WAIT_FAILED)
				return pending(Direction::Output, out) || pending(Direction::Input, out);
		}
	}

	long RingConsumer::producerCpu() const noexcept
	{
		return static_cast<long>(ReadAcquire(&header_->producerCpu));
	}

	void RingConsumer::release(const Acquired& acquired) noexcept
	{
		RingLane& lane = header_->lanes[laneOf(acquired.direction)];
		header_->completeTick[laneOf(acquired.direction)] = static_cast<LONGLONG>(tickNow());
		WriteRelease(&lane.completed, static_cast<LONG>(acquired.sequence));
		if (sync_.done[laneOf(acquired.direction)] != nullptr)
			SetEvent(sync_.done[laneOf(acquired.direction)]);
	}
}
