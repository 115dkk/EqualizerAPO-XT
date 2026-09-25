/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The convolver units of one filter together with their mute bookkeeping
	(audit #348 F3). ConvolutionFilter, MultiConvolutionFilter and
	HilbertFilter each owned an HConvSingleArray and a ConvolverMuteState
	side by side and kept the arm / judge / report / release order by hand.
	The bank keeps that order itself: install() arms with the block size the
	array was built for, admit() judges a block on the audio thread and
	records a mismatch, and finishAndReport() writes the deferred report,
	disarms and releases the units in one call.

	The ConvolverMuteDiagnostics instance and the log prefix stay with each
	filter class and are passed in, so the counts and the report keep the
	filter's own name.
*/

#pragma once

#include <utility>

#include "ConvolverMuteDiagnostics.h"
#include "IrCache.h"

class ConvolverBank
{
public:
	// Takes the units built for frameCount-sample blocks. A null array (the
	// build found nothing to convolve) leaves the bank empty and unarmed.
	void install(HConvSingleArray builtUnits, unsigned frameCount)
	{
		units = std::move(builtUnits);
		if (units != nullptr)
			muteState.arm(frameCount);
	}

	bool installed() const noexcept {return units != nullptr;}

	// Audio thread: whether a block of frameCount samples can be fed to the
	// units. When units are installed but the block size is not the one they
	// were built for, the mute is recorded for the deferred report. RT-safe:
	// relaxed atomics, no I/O.
	bool admit(ConvolverMuteDiagnostics& diagnostics, unsigned frameCount) noexcept
	{
		if (units == nullptr)
			return false;
		if (muteState.shouldMute(frameCount))
		{
			muteState.recordMute(diagnostics, frameCount);
			return false;
		}
		return true;
	}

	// The unit for hcPutSingle/hcProcessSingle/hcGetSingle.
	HConvSingle* unit(unsigned i) const noexcept {return &units[i];}

	// Deferred report through the owning filter's log context, then disarm,
	// then release the units (HConvSingleArray runs the close-then-free
	// sequence). Call from cleanup()/the destructor:
	//   bank.finishAndReport(muteDiagnostics, kPrefix, __FILE__, __LINE__, this);
	void finishAndReport(ConvolverMuteDiagnostics& diagnostics, const wchar_t* logPrefix,
		const char* file, int line, const void* logContext)
	{
		muteState.finishAndReport(diagnostics, logPrefix, file, line, logContext);
		units = nullptr;
	}

private:
	HConvSingleArray units;
	ConvolverMuteState muteState;
};
