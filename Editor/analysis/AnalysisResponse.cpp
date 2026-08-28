/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AnalysisResponse.h"

#include <algorithm>
#include <cmath>

size_t AnalysisResponse::binCountFor(size_t fftSize)
{
	return fftSize == 0 ? 0 : fftSize / 2 + 1;
}

bool AnalysisResponse::isEmpty() const
{
	return bins.empty() || sampleRate == 0 || fftSize == 0;
}

size_t AnalysisResponse::binCount() const
{
	return bins.size();
}

double AnalysisResponse::frequencyOf(size_t index) const
{
	if (fftSize == 0)
		return 0.0;
	return static_cast<double>(index) * sampleRate / static_cast<double>(fftSize);
}

double AnalysisResponse::nyquist() const
{
	return sampleRate / 2.0;
}

size_t AnalysisResponse::nearestBin(double hz) const
{
	if (bins.empty() || sampleRate == 0)
		return 0;
	// Bin spacing is sampleRate / fftSize, so the index is just the frequency
	// divided by it. Rounding picks the nearer of the two neighbours.
	const double exact = hz * static_cast<double>(fftSize) / sampleRate;
	if (!std::isfinite(exact) || exact <= 0.0)
		return 0;
	const double rounded = std::floor(exact + 0.5);
	const double last = static_cast<double>(bins.size() - 1);
	return static_cast<size_t>(std::min(rounded, last));
}

double AnalysisResponse::latencySeconds() const
{
	if (sampleRate == 0)
		return 0.0;
	return latencyFrames / static_cast<double>(sampleRate);
}
