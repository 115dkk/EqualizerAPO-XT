/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ImpulseMeasurement.h"

#include <cfloat>
#include <cmath>

ImpulseMeasurement::ImpulseMeasurement(
	unsigned channelCount,
	int channelIndex,
	int frameCount,
	double* alignedResponse)
	: channelCount(channelCount),
	  channelIndex(channelIndex),
	  frameCount(frameCount),
	  alignedResponse(alignedResponse)
{
}

bool ImpulseMeasurement::addBlock(const double* processed)
{
	if (start != -1)
	{
		// The tail of the previous block went to the front; this block's
		// first frames complete the response.
		for (int i = 0; i < start; i++)
		{
			alignedResponse[frameCount - start + i] = processed[i * channelCount + channelIndex];
		}
		return true;
	}

	for (int i = 0; i < frameCount; i++)
	{
		double s = processed[i * channelCount + channelIndex];
		if (std::abs(s) > 1e-5f)
		{
			start = i;
			break;
		}
	}

	if (start != -1)
	{
		for (int i = 0; i < frameCount - start; i++)
		{
			alignedResponse[i] = processed[(start + i) * channelCount + channelIndex];
		}

		if (start == 0)
			return true;
	}
	else
	{
		silentFrames += frameCount;
	}

	return false;
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
