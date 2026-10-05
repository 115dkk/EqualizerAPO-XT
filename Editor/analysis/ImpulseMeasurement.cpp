/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ImpulseMeasurement.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

ImpulseMeasurement::ImpulseMeasurement(
	unsigned channelCount,
	int channelIndex,
	int responseFrames,
	int blockFrames,
	double* alignedResponse)
	: channelCount(channelCount),
	  channelIndex(channelIndex),
	  responseFrames(responseFrames),
	  blockFrames(blockFrames),
	  alignedResponse(alignedResponse)
{
}

bool ImpulseMeasurement::addBlock(const double* processed)
{
	int copyStart = 0;
	if (start == -1)
	{
		for (int i = 0; i < blockFrames; i++)
		{
			double sample = processed[i * channelCount + channelIndex];
			if (std::abs(sample) > 1e-5f)
			{
				start = i;
				copyStart = i;
				break;
			}
		}

		if (start == -1)
		{
			silentFrames += blockFrames;
			return false;
		}
	}

	const int availableFrames = blockFrames - copyStart;
	const int copyFrames = (std::min)(availableFrames, responseFrames - copiedFrames);
	for (int i = 0; i < copyFrames; i++)
	{
		alignedResponse[copiedFrames + i] =
			processed[(copyStart + i) * channelCount + channelIndex];
	}
	copiedFrames += copyFrames;
	return copiedFrames == responseFrames;
}

bool ImpulseMeasurement::found() const
{
	return start != -1;
}

int ImpulseMeasurement::startFrame() const
{
	return start;
}

int ImpulseMeasurement::latencyFrames() const
{
	return start != -1 ? silentFrames + start : 0;
}

double impulsePeakGainDb(const double (*bins)[2], std::size_t binCount)
{
	double peakGain = -DBL_MAX;

	for (std::size_t i = 0; i < binCount; i++)
	{
		double sqrGain = bins[i][0] * bins[i][0] + bins[i][1] * bins[i][1];
		if (sqrGain > peakGain)
			peakGain = sqrGain;
	}
	peakGain = std::sqrt(peakGain);
	return std::log10(peakGain) * 20.0;
}
