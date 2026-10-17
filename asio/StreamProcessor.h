/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The processor seam of the ASIO wrapper (docs/architecture/asio-host-study.md,
	section 10). An adapter opens a fixed format, supplies each block's plane
	array, processes that block, and closes the stream. The seam knows nothing
	about ASIO, COM, or Qt, so
	the same interface serves the in-process engine adapter the probe measures
	with, the passthrough adapter, and the daemon adapter that ships.

	StreamFormat is fixed-width and pointer-free on purpose: the daemon
	protocol copies it verbatim into the shared session header, and a 32-bit
	wrapper must agree with the 64-bit daemon byte for byte.
*/

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include "asio/AsioConstants.h"

namespace eapo::asio
{
	enum class Direction : uint32_t
	{
		Output = 0,     // DAW -> engine -> target driver
		Input = 1       // target driver -> engine (capture) -> DAW
	};

	constexpr unsigned directionCount = 2;

	enum class Mode : uint32_t
	{
		Sync = 0,       // process() waits for the result inside the buffer switch
		Pipelined = 1   // process() returns the previous block's result; +frames latency
	};

	// Fixed for the life of one open()/close() pair. Every process() carries
	// exactly `frames` frames; a direction with zero channels is disabled and
	// never processed. The two strings are what the engine's Device: line
	// matches (connectionName is the constant L"ASIO").
	struct StreamFormat
	{
		double sampleRate = 0.0;
		uint32_t frames = 0;
		uint32_t channels[directionCount] = {0, 0};
		Mode mode = Mode::Sync;
		uint32_t deadlineUs = 0;          // Sync only; 0 = automatic (a quarter of the period)
		wchar_t deviceName[64] = {};      // target getDriverName()
		wchar_t deviceGuid[40] = {};      // {target CLSID}

		uint32_t channelCount(Direction direction) const noexcept
		{
			return channels[static_cast<uint32_t>(direction)];
		}
	};

	static_assert(sizeof(wchar_t) == 2, "the ring header assumes two-byte wchar_t");
	static_assert(std::is_standard_layout_v<StreamFormat> && std::is_trivially_copyable_v<StreamFormat>,
		"StreamFormat must remain a plain wire type");
	// 8 + 4 + 8 + 4 + 4 + 128 + 80 = 236 payload bytes, padded to the double's
	// 8-byte alignment. Pinned so a field added on one side of the daemon
	// protocol cannot go unnoticed on the other.
	static_assert(sizeof(StreamFormat) == 240, "StreamFormat must stay fixed-width; it is copied into the daemon session header");

	// Optional timing information a processor may expose to diagnostics. The
	// wrapper does not inspect it; adapters that have no handoff return nullptr.
	struct HandoffProfile
	{
		uint32_t deadlineUs = 0;
		uint32_t maxDispatchUs = 0;
		uint32_t maxServiceUs = 0;
		uint32_t maxReturnUs = 0;
		uint64_t roundTripBuckets[6] = {0, 0, 0, 0, 0, 0};   // <100, <200, <500, <1000, <2000, >=2000 us
		uint64_t completed = 0;
	};

	enum class Outcome : uint32_t
	{
		Processed,      // the planes hold processed audio to write back
		LateHeld,       // the planes hold delayed original audio; keep the adapter's latency
		LateDropped,    // no result is safe to read; write the current original audio
		Gone,           // the processor is dead for the rest of the stream; original audio
		Off             // this direction is not processed by this adapter; original audio
	};

	// Written by the buffer-switch thread only and handed to close(), where
	// the adapter publishes it. The wrapper measures process() wall time;
	// that is the only cost it can see.
	struct StreamStats
	{
		uint64_t blocks[directionCount] = {0, 0};
		uint64_t late[directionCount] = {0, 0};
		uint64_t gone[directionCount] = {0, 0};
		uint32_t maxProcessUs[directionCount] = {0, 0};
		uint32_t lastProcessUs[directionCount] = {0, 0};
		uint32_t staleBlocks = 0;         // passed through between sampleRateDidChange and the DAW's reset
	};

	struct OpenReport
	{
		enum class Status : uint32_t
		{
			Ok,
			Unavailable,    // the engine host could not be reached or did not become ready
			Rejected        // this adapter cannot serve this format
		};

		Status status = Status::Unavailable;
		uint32_t extraLatencyFrames = 0;  // 0 for Sync, frames for Pipelined
		char message[errorMessageBytes] = {};           // reported through getErrorMessage when status != Ok
	};

	// The value both the registry record and the probe's command line fill.
	// Pipelined is the default: on real hardware at 64 frames the sync
	// handoff completes under 100 us 99.96 percent of the time, and the rest
	// is the OS preempting one of the two threads for up to two
	// milliseconds, which sync mode can only pass through. One buffer of
	// latency buys immunity to that; sync stays available for those who
	// would rather hear an occasional unprocessed block than add it.
	struct StreamOptions
	{
		bool processOutput = true;
		bool processInput = true;
		Mode mode = Mode::Pipelined;
		uint32_t deadlineUs = 0;          // 0 = automatic: deadlinePercent of the period
		uint32_t deadlinePercent = 0;     // Sync only; 0 = 25. The Device Selector offers 25, 50 and 75
		uint32_t readyTimeoutMs = 20000;  // a cold start loads config.txt, which may hold convolution IRs
		uint32_t lingerMs = 60000;        // how long the daemon outlives its last stream
		std::wstring configPath;          // empty = registry ConfigPath + watcher (production)
		std::wstring daemonExePath;       // empty = EqualizerAPOHost.exe beside the wrapper DLL
		std::wstring daemonEndpoint;      // empty = per-session default; the probe pins a name
	};

	// One buffer period of a format in microseconds, or 0 without a rate.
	// The wrapper, the daemon adapter and the engine host derive their
	// deadlines and spin budgets from this one computation.
	inline double periodUs(const StreamFormat& format) noexcept
	{
		if (format.sampleRate <= 0.0)
			return 0.0;
		return static_cast<double>(format.frames) * 1000000.0 / format.sampleRate;
	}

	// The synchronous deadline for a format: an explicit microsecond budget
	// wins; otherwise the share of the buffer period the options name.
	inline uint32_t syncDeadlineUs(const StreamFormat& format, const StreamOptions& options) noexcept
	{
		if (options.deadlineUs != 0)
			return options.deadlineUs;
		if (format.sampleRate <= 0.0)
			return 0;
		const uint32_t percent = options.deadlinePercent != 0 ? options.deadlinePercent : defaultDeadlinePercent;
		return static_cast<uint32_t>(periodUs(format) * static_cast<double>(percent) / 100.0);
	}

	class IStreamProcessor
	{
	public:
		virtual ~IStreamProcessor() = default;

		// DAW control thread. Returns once the first process() can be served
		// by an engine holding the current configuration (the readiness
		// barrier), or once the adapter gives up. Never called from init():
		// hosts init every listed driver just to enumerate them.
		virtual OpenReport open(const StreamFormat& format, const StreamOptions& options) = 0;

		// The target driver's buffer-switch thread asks for the planes of this
		// block before converting into them. The returned array and every plane
		// stay valid through process(); process() may change the pointers in the
		// array to hand a different buffer back without copying samples. No
		// allocation, C++ locks or exceptions in either call.
		virtual float** planes(Direction direction) noexcept = 0;

		// Once per enabled direction per switch, after planes() was filled.
		// Waiting on a kernel object inside the deadline is allowed. Processed
		// and LateHeld leave output in that same pointer array; every other
		// outcome makes the caller use the current original audio.
		virtual Outcome process(Direction direction) noexcept = 0;

		// Control thread, after the target's disposeBuffers() returned so no
		// buffer switch can arrive. Idempotent.
		virtual void close() noexcept = 0;

		virtual const HandoffProfile* profile() const noexcept
		{
			return nullptr;
		}
	};

	// The adapter used when neither direction is processed, and the floor
	// every other adapter degrades to.
	class PassthroughProcessor final : public IStreamProcessor
	{
	public:
		OpenReport open(const StreamFormat&, const StreamOptions&) override
		{
			OpenReport report;
			report.status = OpenReport::Status::Ok;
			return report;
		}

		float** planes(Direction) noexcept override
		{
			return nullptr;
		}

		Outcome process(Direction) noexcept override
		{
			return Outcome::Off;
		}

		void close() noexcept override
		{
		}
	};
}
