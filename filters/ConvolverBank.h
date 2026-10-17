/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The convolver units of one filter together with their mute bookkeeping
	(audit #422 E3). ConvolutionFilter, MultiConvolutionFilter and HilbertFilter
	each used to carry a second state object and pass diagnostics through every
	bank call. The bank now owns the complete arm / judge / report / release
	state machine; only the process-wide counters remain external.
*/

#pragma once

#include <source_location>
#include <utility>

#include "ConvolverArray.h"
#include "ConvolverMuteDiagnostics.h"

class ConvolverBank
{
public:
	ConvolverBank(ConvolverMuteDiagnostics& diagnostics, const wchar_t* logPrefix)
		: diagnostics(diagnostics), logPrefix(logPrefix)
	{
	}

	// Takes the units built for frameCount-sample blocks. An empty array (the
	// build found nothing to convolve) leaves the bank empty and unarmed.
	void install(HConvSingleArray builtUnits, unsigned frameCount)
	{
		units = std::move(builtUnits);
		if (units)
		{
			initializedFrameCount = frameCount;
			mismatchSeen = false;
		}
	}

	bool installed() const noexcept {return static_cast<bool>(units);}

	// Audio thread: whether a block of frameCount samples can be fed to the
	// units. A mismatch is recorded for the deferred report. RT-safe: relaxed
	// atomics and no I/O.
	bool admit(unsigned frameCount) noexcept
	{
		if (!units)
			return false;
		if (frameCount != initializedFrameCount)
		{
			const bool firstMismatch = !mismatchSeen;
			mismatchSeen = true;
			diagnostics.recordMute(frameCount, firstMismatch);
			return false;
		}
		return true;
	}

	// The unit for hcPutSingle/hcProcessSingle/hcGetSingle.
	HConvSingle* unit(unsigned i) const noexcept {return &units[i];}

	// Deferred report through the bank's owning filter context, then disarm and
	// release the units. source_location keeps the calling filter's file and line
	// in the diagnostic without making every call carry protocol arguments.
	void finishAndReport(
		const std::source_location& location = std::source_location::current())
	{
		if (mismatchSeen)
		{
			Logging::log(location.file_name(), static_cast<int>(location.line()), this,
				false, kConvolverMuteReportFormat, logPrefix,
				diagnostics.firstMuteFrameCount.load(std::memory_order_relaxed),
				initializedFrameCount,
				diagnostics.muteCallCount.load(std::memory_order_relaxed));
		}
		mismatchSeen = false;
		initializedFrameCount = 0;
		units.reset();
	}

private:
	ConvolverMuteDiagnostics& diagnostics;
	const wchar_t* logPrefix;
	HConvSingleArray units;
	unsigned initializedFrameCount = 0;
	bool mismatchSeen = false;
};
