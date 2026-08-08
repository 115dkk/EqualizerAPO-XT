/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Shared mute-path diagnostics for the convolver family (audit #250 A4).
	When process() sees a block size other than the one the convolver was
	initialized for, it mutes rather than re-initializing on the audio
	thread; Convolution and MultiConvolution each carried a verbatim copy of
	the bookkeeping (two relaxed atomics bumped on the RT path, one deferred
	log line in cleanup()) and Hilbert, the third convolver, muted with no
	diagnostics at all. One type, three filters.

	The atomics are per-filter-class (one instance per translation unit,
	like the statics they replace) and process-wide: the engine hands every
	convolver of a configuration the same block size, so the counts describe
	the process, while the first-mismatch flag stays per filter instance so
	each instance reports once. The report itself is written by the filter's
	own cleanup() through LogF, so the log context remains the filter's.
*/

#pragma once

#include <atomic>

struct ConvolverMuteDiagnostics
{
	// Running total of process() calls that took the mute path, plus the
	// first mismatching block size seen. RT-safe: relaxed bumps, no I/O.
	std::atomic<unsigned long long> muteCallCount{ 0 };
	std::atomic<unsigned> firstMuteFrameCount{ 0 };

	void recordMute(unsigned frameCount, bool& firstMismatchSeen) noexcept
	{
		muteCallCount.fetch_add(1, std::memory_order_relaxed);
		if (!firstMismatchSeen)
		{
			// Keep the block size that first failed; the deferred report needs it.
			firstMuteFrameCount.store(frameCount, std::memory_order_relaxed);
			firstMismatchSeen = true;
		}
	}
};

// One wording for the deferred report; each filter passes its own prefix
// constant (pinned by HybridConvTests) and its initialized block size.
constexpr const wchar_t* kConvolverMuteReportFormat =
	L"%s %u differs from initialized %u; output muted (audio-thread re-init skipped) [mute calls: %llu total]";
