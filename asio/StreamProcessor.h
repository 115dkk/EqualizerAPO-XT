/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The processor seam of the ASIO wrapper (docs/architecture/asio-host-study.md,
	section 10). Everything an adapter must implement is three calls: open the
	stream for a fixed format, transform one direction's planes in place per
	buffer switch, close it. The seam knows nothing about ASIO, COM, or Qt, so
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

	enum class Outcome : uint32_t
	{
		Processed,      // the planes hold the audio to write back
		Late,           // the deadline passed; the caller writes the original audio
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
		// Per direction: an array of channelCount(direction) pointers to
		// `frames` float32 samples each, valid from open() until close().
		// nullptr means the adapter does not process that direction; the
		// wrapper then copies the original samples without conversion.
		float** planes[directionCount] = {nullptr, nullptr};
		uint32_t extraLatencyFrames = 0;  // 0 for Sync, frames for Pipelined
		char message[124] = {};           // reported through getErrorMessage when status != Ok
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

	// The synchronous deadline for a format: an explicit microsecond budget
	// wins; otherwise the share of the buffer period the options name.
	inline uint32_t syncDeadlineUs(const StreamFormat& format, const StreamOptions& options) noexcept
	{
		if (options.deadlineUs != 0)
			return options.deadlineUs;
		if (format.sampleRate <= 0.0)
			return 0;
		const double periodUs = static_cast<double>(format.frames) * 1000000.0 / format.sampleRate;
		const uint32_t percent = options.deadlinePercent != 0 ? options.deadlinePercent : 25;
		return static_cast<uint32_t>(periodUs * static_cast<double>(percent) / 100.0);
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

		// The target driver's buffer-switch thread. Once per enabled direction
		// per switch. Transforms planes[direction] in place. No allocation, no
		// C++ locks, no exceptions; waiting on a kernel object inside the
		// deadline is allowed. After anything but Processed the plane contents
		// are unspecified and the caller writes the original audio instead.
		virtual Outcome process(Direction direction) noexcept = 0;

		// Control thread, after the target's disposeBuffers() returned so no
		// buffer switch can arrive. Idempotent.
		virtual void close(const StreamStats& stats) noexcept = 0;
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

		Outcome process(Direction) noexcept override
		{
			return Outcome::Off;
		}

		void close(const StreamStats&) noexcept override
		{
		}
	};
}
