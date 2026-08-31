/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	StreamRing: the realtime handoff between the ASIO wrapper (in a DAW) and
	the engine host (docs/architecture/asio-host-study.md, section 10.2). One
	shared region holds a fixed-width header and, per direction (lane), two
	slots of float32 planes. The producer publishes a sequence number into a
	lane and signals; the consumer processes the slot in place, records the
	sequence as completed and signals back. Everything the two sides agree
	on is bytes at fixed offsets - no pointers, so a 32-bit wrapper and a
	64-bit host see the same region - and the kernel objects are handed in
	by whoever created them (a named set across processes, an unnamed set
	inside one process for the tests).

	Protocol, per lane:
	  * The producer may publish `seq` only when the consumer has completed
	    `seq - 2` or later, i.e. the slot `seq & 1` is no longer being read.
	    If not, the block is dropped without touching the region.
	  * publish(seq) stores the sequence with release semantics and sets the
	    lane's work event.
	  * wait(seq, budget) returns Done when `completed >= seq`, Late when the
	    budget passes first, Gone when the peer handle signals or the state
	    leaves Ready. poll(seq, budget) returns the same results without a
	    kernel wait. A late block is still processed by the consumer in order
	    (filter state stays continuous); only the producer stops waiting for it.
	  * The consumer takes every published sequence in order, never skips.

	Readiness: the producer formats the header (state Announced) and waits on
	the ready event; the consumer validates the layout, creates its engines,
	and writes Ready (or Fault with a code) before signalling. close() writes
	Closing and wakes the consumer so it can tear down.

	Memory ordering uses the Win32 acquire/release intrinsics on LONG fields;
	the header is therefore made of LONGs where both sides race.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "asio/StreamProcessor.h"

namespace eapo::ipc
{
	constexpr uint32_t ringMagic = 0x52504145u;      // 'EAPR'
	constexpr uint32_t ringLayoutVersion = 1;
	constexpr size_t ringHeaderBytes = 512;
	constexpr size_t ringAlignment = 64;
	// ASIO hardware stays far below these bounds. They keep the complete ring
	// under 128 MiB, so its uint32_t wire geometry cannot wrap.
	constexpr uint32_t maxRingChannels = 64;
	constexpr uint32_t maxRingFrames = 65536;

	enum class RingState : uint32_t
	{
		Empty = 0,
		Announced = 1,   // header written by the producer, no consumer yet
		Ready = 2,       // consumer holds initialized engines for this format
		Closing = 3,     // producer is leaving
		Fault = 4        // consumer could not serve; faultCode says why
	};

	enum class RingFault : uint32_t
	{
		None = 0,
		LayoutMismatch = 1,
		EngineFailed = 2,
		HostShuttingDown = 3
	};

	struct RingLane
	{
		LONG sequence;          // producer: last published, starts at 0 (nothing yet)
		LONG completed;         // consumer: last completed
		uint32_t slotOffset[2]; // bytes from the region base
		uint32_t slotBytes;
	};

	struct RingHeader
	{
		uint32_t magic;
		uint32_t layoutVersion;
		uint32_t totalBytes;
		uint32_t reserved0;
		eapo::asio::StreamFormat format;   // 8-byte aligned at offset 16
		uint32_t producerPid;
		uint32_t consumerPid;
		LONG state;                        // RingState
		LONG faultCode;                    // RingFault
		RingLane lanes[eapo::asio::directionCount];
		// QueryPerformanceCounter stamps, one clock for both processes:
		// when the producer published, when the consumer picked the block
		// up, when it finished. The producer reads them after Done to tell a
		// slow wake-up from slow processing from a slow return.
		LONGLONG publishTick[eapo::asio::directionCount];
		LONGLONG acquireTick[eapo::asio::directionCount];
		LONGLONG completeTick[eapo::asio::directionCount];
		// The processor the producer last published from. Two real-time
		// threads spinning on one core serialize on the scheduler quantum,
		// so the consumer keeps its thread off this one.
		LONG producerCpu;
		uint8_t reserved[ringHeaderBytes - (16 + sizeof(eapo::asio::StreamFormat) + 16 + 2 * sizeof(RingLane) + 48 + 4)];
	};

	static_assert(std::is_standard_layout_v<RingLane> && std::is_trivially_copyable_v<RingLane>,
		"RingLane must remain a plain wire type");
	static_assert(sizeof(RingLane) == 20, "RingLane must stay fixed-width");
	static_assert(std::is_standard_layout_v<RingHeader> && std::is_trivially_copyable_v<RingHeader>,
		"RingHeader must remain a plain wire type");
	static_assert(sizeof(RingHeader) == ringHeaderBytes, "RingHeader must be exactly 512 bytes");
	static_assert(offsetof(RingHeader, format) == 16, "StreamFormat sits at offset 16");
	static_assert(offsetof(RingHeader, lanes) == 16 + sizeof(eapo::asio::StreamFormat) + 16, "lanes follow the state words");

	// Sizes both sides derive from the format alone.
	namespace RingGeometry
	{
		bool validFormat(const eapo::asio::StreamFormat& format) noexcept;
		uint32_t slotBytes(const eapo::asio::StreamFormat& format, eapo::asio::Direction direction) noexcept;
		uint32_t totalBytes(const eapo::asio::StreamFormat& format) noexcept;
	}

	// The kernel objects a side uses. Not owned; the creator keeps them alive
	// for as long as the ring is in use. `peer` is a handle that signals when
	// the other side is gone (its process handle across processes, an event
	// the test sets inside one).
	struct RingSync
	{
		HANDLE work[eapo::asio::directionCount] = {nullptr, nullptr};   // auto-reset
		HANDLE done[eapo::asio::directionCount] = {nullptr, nullptr};   // auto-reset
		HANDLE ready = nullptr;                                           // manual-reset
		HANDLE peer = nullptr;
	};

	enum class RingWait : uint32_t
	{
		Done,
		Late,
		Gone
	};

	class RingProducer
	{
	public:
		// Formats the header for `format` over a region of at least
		// RingGeometry::totalBytes(format) bytes and marks it Announced.
		RingProducer(void* base, const eapo::asio::StreamFormat& format, uint32_t producerPid, const RingSync& sync) noexcept;

		// Blocks until the consumer writes Ready or Fault, the peer signals,
		// or the timeout passes. Returns the state seen.
		RingState waitReady(uint32_t timeoutMs) noexcept;
		RingFault fault() const noexcept;
		uint32_t consumerPid() const noexcept;

		// True when slot `seq & 1` is free: the consumer completed seq - 2 or
		// later (or nothing was published yet). Buffer-switch thread.
		bool canPublish(eapo::asio::Direction direction, uint32_t seq) const noexcept;
		// The slot's planes, channel-major, `frames` floats per channel.
		float* slot(eapo::asio::Direction direction, uint32_t seq) noexcept;
		void publish(eapo::asio::Direction direction, uint32_t seq) noexcept;
		// budgetUs == 0 answers immediately (Done only if already completed).
		RingWait wait(eapo::asio::Direction direction, uint32_t seq, uint32_t budgetUs) noexcept;
		// Polls with pauses only; never enters a kernel wait.
		RingWait poll(eapo::asio::Direction direction, uint32_t seq, uint32_t budgetUs) noexcept;
		bool completed(eapo::asio::Direction direction, uint32_t seq) const noexcept;
		bool peerGone() const noexcept;

		void close() noexcept;
		RingState state() const noexcept;

		// The last completed handoff on a lane, in microseconds: how long the
		// consumer took to pick the block up after publish (dispatch), how
		// long it held it (service), and how long its completion took to be
		// seen here (return, measured against now).
		struct HandoffTiming
		{
			uint32_t dispatchUs = 0;
			uint32_t serviceUs = 0;
			uint32_t returnUs = 0;
		};
		HandoffTiming lastHandoff(eapo::asio::Direction direction) const noexcept;

	private:
		RingHeader* header_;
		RingSync sync_;
		double ticksPerMicro_ = 0.0;
	};

	class RingConsumer
	{
	public:
		// Validates the header; valid() is false on a magic, version or size
		// mismatch, in which case nothing else may be called but setState.
		RingConsumer(void* base, size_t bytes, const RingSync& sync) noexcept;

		bool valid() const noexcept {return valid_;}
		const RingHeader& header() const noexcept {return *header_;}
		const eapo::asio::StreamFormat& format() const noexcept {return header_->format;}

		void setConsumerPid(uint32_t pid) noexcept;
		// Ready and Fault also signal the ready event.
		void setState(RingState state, RingFault fault = RingFault::None) noexcept;
		RingState state() const noexcept;

		struct Acquired
		{
			eapo::asio::Direction direction;
			uint32_t sequence;
			float* slot;
		};

		// Waits for work on either lane, the peer, or the timeout. Lanes are
		// served in order, output first when both are pending. False means
		// nothing to do: timeout, Closing, or the peer went away (check
		// state() and peerGone()). spinUs is how long to poll before the
		// kernel wait: a woken thread pays scheduler and C-state latency
		// that a spinning one does not, and inside one buffer period that
		// latency is most of a sync deadline.
		bool acquire(Acquired& out, uint32_t timeoutMs, uint32_t spinUs = 0) noexcept;
		void release(const Acquired& acquired) noexcept;
		bool peerGone() const noexcept {return peerGone_;}
		// The processor the producer last published from, or -1.
		long producerCpu() const noexcept;

	private:
		bool pending(eapo::asio::Direction direction, Acquired& out) noexcept;

		RingHeader* header_;
		RingSync sync_;
		bool valid_ = false;
		bool peerGone_ = false;
		double ticksPerMicro_ = 0.0;
	};
}
