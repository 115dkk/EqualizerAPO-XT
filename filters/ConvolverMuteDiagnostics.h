/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Shared process-wide counters for the convolver family's mute diagnostics.
	The per-filter state machine lives in ConvolverBank; these atomics remain one
	instance per filter class so the existing totals and log messages stay the
	same. The audio thread only performs relaxed atomic updates here.
*/

#pragma once

#include <atomic>

#include "services/logging/Logging.h"

struct ConvolverMuteDiagnostics
{
	std::atomic<unsigned long long> muteCallCount{0};
	std::atomic<unsigned> firstMuteFrameCount{0};

	void recordMute(unsigned frameCount, bool firstMismatch) noexcept
	{
		muteCallCount.fetch_add(1, std::memory_order_relaxed);
		if (firstMismatch)
			firstMuteFrameCount.store(frameCount, std::memory_order_relaxed);
	}
};

// One wording for the deferred report. HybridConvTests pins each filter's
// prefix, and ConvolverBank supplies the initialized and mismatching sizes.
constexpr const wchar_t* kConvolverMuteReportFormat =
	L"%s %u differs from initialized %u; output muted (audio-thread re-init skipped) [mute calls: %llu total]";
