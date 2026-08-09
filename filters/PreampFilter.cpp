/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <cmath>

#include "hwy/highway.h"

#include "PreampFilter.h"
#include "diagnostics/performance/PerfProfile.h"

namespace hn = hwy::HWY_NAMESPACE;

using std::max;
using std::pow;
using std::vector;
using std::wstring;

PreampFilter::PreampFilter(double dbGain)
	: dbGain(dbGain), gain(0.0), channelCount(0)
{
	// Calculate the linear gain factor from dB value
	gain = pow(10.0, this->dbGain / 20.0);
}

vector<wstring> PreampFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	this->channelCount = channelNames.size();
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void PreampFilter::process(double** output, double** input, unsigned frameCount)
{
    PerfScope _ps("PreampFilter::process");
    // The gain factor is constant for all samples, load it once.
    const double gainFactor = this->gain;

    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto gainVec = hn::Set(d, gainFactor);

    // Process each channel independently. One portable Highway loop compiles to
    // the widest target enabled for this build (NEON on ARM64).
    for (size_t c = 0; c < channelCount; ++c)
    {
        const double* inputChannel = input[c];
        double* outputChannel = output[c];

        size_t i = 0;
        for (; i + N <= frameCount; i += N)
        {
            const auto samples = hn::LoadU(d, inputChannel + i);
            hn::StoreU(hn::Mul(samples, gainVec), d, outputChannel + i);
        }
        // Scalar tail (also covers frameCount < N).
        for (; i < frameCount; ++i)
            outputChannel[i] = inputChannel[i] * gainFactor;
    }
}
#pragma AVRT_CODE_END
