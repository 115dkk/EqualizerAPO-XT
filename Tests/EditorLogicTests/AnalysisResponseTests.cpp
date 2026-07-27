/*
	This file is part of EqualizerAPO-XT.

	AnalysisResponse bin arithmetic and empty-response semantics.

	The analysis thread hands this object to the UI and nothing else describes
	the frequency axis, so an off-by-one here would move every curve the graph
	draws. The DC and Nyquist ends are the two the arithmetic is easiest to get
	wrong, and they are also the two ends the phase and group-delay views have
	to treat specially, so both are pinned explicitly.
*/

#include <cmath>

#include "Editor/analysis/AnalysisResponse.h"

#include "EditorLogicTestSupport.h"

namespace
{
AnalysisResponse makeResponse(unsigned sampleRate, size_t fftSize)
{
	AnalysisResponse response;
	response.sampleRate = sampleRate;
	response.fftSize = fftSize;
	response.bins.assign(AnalysisResponse::binCountFor(fftSize), {});
	return response;
}
}

void testAnalysisResponseBinArithmetic()
{
	// A real-to-complex transform of N samples produces N/2 + 1 bins: DC, the
	// N/2 - 1 interior bins, and Nyquist. This is the count the analysis thread
	// allocates and copies, so it is worth stating as a fact rather than a
	// formula spelled out in three places.
	expectEqual(static_cast<int>(AnalysisResponse::binCountFor(0)), 0,
		"a zero-length transform has no bins");
	expectEqual(static_cast<int>(AnalysisResponse::binCountFor(2)), 2,
		"a 2-point transform has DC and Nyquist only");
	expectEqual(static_cast<int>(AnalysisResponse::binCountFor(65536)), 32769,
		"the default resolution keeps 32769 bins");

	const AnalysisResponse response = makeResponse(48000, 65536);
	expectEqual(static_cast<int>(response.binCount()), 32769,
		"the response carries one bin per transform output");

	expectTrue(response.frequencyOf(0) == 0.0, "bin 0 is DC");
	expectTrue(std::abs(response.frequencyOf(response.binCount() - 1) - 24000.0) < 1e-9,
		"the last bin is Nyquist");
	expectTrue(std::abs(response.nyquist() - 24000.0) < 1e-9, "Nyquist is half the sample rate");
	// Bin spacing at 48 kHz / 65536 is 0.732421875 Hz exactly.
	expectTrue(std::abs(response.frequencyOf(1) - 48000.0 / 65536.0) < 1e-12,
		"bin spacing is sampleRate / fftSize");

	// nearestBin rounds rather than truncating, so a frequency sitting between
	// two bins picks the closer one instead of always the lower.
	expectEqual(static_cast<int>(response.nearestBin(0.0)), 0, "DC maps to bin 0");
	expectEqual(static_cast<int>(response.nearestBin(1000.0)), 1365,
		"1 kHz maps to the nearest bin, not the one below it");
	expectTrue(std::abs(response.frequencyOf(response.nearestBin(1000.0)) - 1000.0) < 0.4,
		"the bin nearest 1 kHz is within half a bin of it");

	// Out-of-range inputs clamp rather than indexing past the end. The graph
	// asks for 20 Hz and 20 kHz regardless of the sample rate, and at a low
	// sample rate 20 kHz can be above Nyquist.
	expectEqual(static_cast<int>(response.nearestBin(-100.0)), 0,
		"a negative frequency clamps to DC");
	expectEqual(static_cast<int>(response.nearestBin(1e9)), 32768,
		"a frequency past Nyquist clamps to the last bin");
	expectEqual(static_cast<int>(response.nearestBin(std::nan(""))), 0,
		"a non-finite frequency clamps to DC rather than indexing wildly");
}

void testAnalysisResponseEmptyAndLatency()
{
	// Three ways to be empty, all of which the graph has to survive: before
	// the first analysis, after a failed one, and the degenerate case of a
	// response that carries bins but no sample rate to place them on.
	AnalysisResponse untouched;
	expectTrue(untouched.isEmpty(), "a default-constructed response is empty");
	expectFalse(untouched.frozenDynamicResponse,
		"a default response is not labelled as a frozen dynamic snapshot");
	expectEqual(static_cast<int>(untouched.binCount()), 0, "an empty response has no bins");
	expectTrue(untouched.frequencyOf(5) == 0.0, "an empty response reports no frequency");
	expectEqual(static_cast<int>(untouched.nearestBin(1000.0)), 0,
		"an empty response answers bin 0 rather than indexing nothing");
	expectTrue(untouched.latencySeconds() == 0.0, "an empty response has no latency");

	AnalysisResponse noRate = makeResponse(0, 1024);
	expectTrue(noRate.isEmpty(), "bins without a sample rate cannot be placed, so the response is empty");

	AnalysisResponse populated = makeResponse(48000, 1024);
	expectFalse(populated.isEmpty(), "a populated response is not empty");
	populated.frozenDynamicResponse = true;
	AnalysisResponse copied = populated;
	expectTrue(copied.frozenDynamicResponse,
		"the frozen-dynamic analysis marker survives response publication");

	// The analyzer strips leading silence and reports it separately, so the
	// bins describe the config with its bulk delay removed. A view that wants
	// the delay back needs this converted to time.
	populated.latencyFrames = 480;
	expectTrue(std::abs(populated.latencySeconds() - 0.01) < 1e-12,
		"480 frames at 48 kHz is 10 ms of stripped latency");
	populated.latencyFrames = 0;
	expectTrue(populated.latencySeconds() == 0.0, "no stripped frames means no latency to reapply");
}
