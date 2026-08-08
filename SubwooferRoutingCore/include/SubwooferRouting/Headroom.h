// SPDX-License-Identifier: MIT

#pragma once

#include "State.h"

#include <cstddef>
#include <string>
#include <vector>

namespace subroute
{

inline constexpr std::size_t kHeadroomFrequencySampleCount = 2048;
inline constexpr double kHeadroomMinimumFrequencyHz = 20.0;

struct OutputHeadroomResult
{
	std::string outputChannelId;
	double predictedPeakLinearBeforeTrim = 0.0;
	double predictedPeakDbBeforeTrim = 0.0;
	double criticalFrequencyHz = 0.0;
};

struct HeadroomAnalysis
{
	HeadroomMode mode = HeadroomMode::Auto;
	double appliedTrimDb = 0.0;
	double appliedTrimLinear = 1.0;
	double predictedPeakLinearBeforeTrim = 0.0;
	double predictedPeakDbBeforeTrim = 0.0;
	std::string criticalOutputChannelId;
	double criticalFrequencyHz = 0.0;
	std::size_t frequencySampleCount = 0;
	std::vector<OutputHeadroomResult> outputs;
};

/*
	Auto analysis evaluates kHeadroomFrequencySampleCount logarithmically
	spaced frequencies, including 20 Hz and Nyquist.

	For output o, physical input i, path p, and frequency f, the transfer from
	input i through all paths to output o is coherently summed as a complex
	value. The worst-case coherent multi-input magnitude is then:

		sum_i(abs(sum_p(outputGain[o,p] * pathResponse[p,f] *
			sourceMixGain[p,i])))

	For an outputMatrix entry in Add mode, the original target-channel input
	contributes a direct unity transfer. Replace mode has no direct transfer.

	The common automatic trim applied to every matrix-targeted output is:

		min(0 dB, -20 * log10(maximumPredictedMagnitude))

	Manual mode uses manualTrimDb instead. Untouched channels are excluded
	from analysis and are not trimmed.
*/

}
