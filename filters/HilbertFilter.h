/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <vector>

#include "dsp/DelayLine.h"
#include "engine/IFilter.h"
#include "filters/ConvolverBank.h"
#include "filters/HilbertCommand.h"
#include "filters/IrCache.h"

constexpr unsigned HilbertTapCount = 1025;
constexpr unsigned HilbertLatencySamples = (HilbertTapCount - 1) / 2;

// Exposed for deterministic command/DSP tests and for the Editor readout.
std::vector<double> designHilbertFir(int directionDegrees);

#pragma AVRT_VTABLES_BEGIN
class HilbertFilter : public IFilter
{
public:
	explicit HilbertFilter(const HilbertCommand& command);
	~HilbertFilter();
	// The deferred mute diagnostic's prefix is part of the filter's
	// observable contract (HybridConvTests pins it), like the other
	// convolvers' (audit #250 A4 - Hilbert used to mute silently).
	static constexpr const wchar_t* kFrameCountMismatchLogPrefix =
		L"HilbertFilter: frameCount";
	bool getAllChannels() override { return true; }
	bool getInPlace() override { return false; }
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount,
		std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	HilbertCommand command;
	std::vector<double> coefficients;
	ConvolverBank bank;
	std::vector<int> shifted;
	std::vector<int> aligned;
	// The aligned channels, delayed by HilbertLatencySamples to line up with
	// the shifted ones; the pointer arrays are sized in initialize().
	DelayLine alignedDelay;
	std::vector<double*> alignedOutputs;
	std::vector<const double*> alignedInputs;
	unsigned channelCount = 0;
};
#pragma AVRT_VTABLES_END
