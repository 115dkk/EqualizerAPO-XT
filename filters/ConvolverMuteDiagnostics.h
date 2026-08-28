/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

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

#include "services/logging/Logging.h"

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

// Per-instance side of the bookkeeping. Before audit #275 (A4/TD-27) the
// choreography around the atomics - the initialized-block-size member, the
// mute branch, the deferred report in cleanup, and the report-before-teardown
// ordering - was repeated verbatim by the three convolver filters, with the
// ordering held up only by comments. This object owns it: arm() records the
// initialized size, shouldMute()/recordMute() serve the RT path, and
// finishAndReport() both writes the deferred report and disarms, so member
// teardown order no longer matters.
class ConvolverMuteState
{
public:
	void arm(unsigned frameCount) noexcept
	{
		initializedFrameCount_ = frameCount;
		mismatchSeen = false;
	}

	unsigned initializedFrameCount() const noexcept {return initializedFrameCount_;}

	bool shouldMute(unsigned frameCount) const noexcept
	{
		return frameCount != initializedFrameCount_;
	}

	// RT-safe: relaxed bumps, no I/O (logging would open/write/close the log
	// file on the audio thread exactly when the stream can least afford it).
	void recordMute(ConvolverMuteDiagnostics& diagnostics, unsigned frameCount) noexcept
	{
		diagnostics.recordMute(frameCount, mismatchSeen);
	}

	// Deferred report through the owning filter's log context, then disarm.
	// Call from cleanup()/the destructor:
	//   muteState.finishAndReport(muteDiagnostics, kPrefix, __FILE__, __LINE__, this);
	void finishAndReport(ConvolverMuteDiagnostics& diagnostics, const wchar_t* logPrefix,
		const char* file, int line, const void* logContext)
	{
		if (mismatchSeen)
			Logging::log(file, line, logContext, false, kConvolverMuteReportFormat, logPrefix,
				diagnostics.firstMuteFrameCount.load(std::memory_order_relaxed),
				initializedFrameCount_,
				diagnostics.muteCallCount.load(std::memory_order_relaxed));
		mismatchSeen = false;
		initializedFrameCount_ = 0;
	}

private:
	unsigned initializedFrameCount_ = 0;
	bool mismatchSeen = false;
};
