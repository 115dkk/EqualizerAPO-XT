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
#include "BiQuadFilter.h"
#include "BiQuadKernelPlan.h"
#include "diagnostics/performance/PerfProfile.h"

#include "hwy/highway.h"

namespace hn = hwy::HWY_NAMESPACE;

BiQuadFilter::BiQuadFilter(BiQuad::Type type, double dbGain, double freq, double bandwidthOrQOrS, bool isBandwidthOrS, bool isCornerFreq)
    : type(type), dbGain(dbGain), freq(freq), bandwidthOrQOrS(bandwidthOrQOrS), isBandwidthOrS(isBandwidthOrS), isCornerFreq(isCornerFreq), channelCount(0)
{
}

BiQuad::Type BiQuadFilter::getType() const
{
    return type;
}

double BiQuadFilter::getDbGain() const
{
    return dbGain;
}

double BiQuadFilter::getFreq() const
{
    return freq;
}

double BiQuadFilter::getBandwidthOrQOrS() const
{
    return bandwidthOrQOrS;
}

bool BiQuadFilter::getIsBandwidthOrS() const
{
    return isBandwidthOrS;
}

bool BiQuadFilter::getIsCornerFreq() const
{
    return isCornerFreq;
}

std::vector<std::wstring> BiQuadFilter::initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
    this->channelCount = channelNames.size();

    a0.resize(channelCount); a1.resize(channelCount); a2.resize(channelCount);
    b1.resize(channelCount); b2.resize(channelCount);
    x1.assign(channelCount, 0.0); x2.assign(channelCount, 0.0);
    y1.assign(channelCount, 0.0); y2.assign(channelCount, 0.0);

    double biquadFreq = freq;
    if (isCornerFreq && (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF))
    {
        double s = bandwidthOrQOrS;
        if (!isBandwidthOrS) // Q
        {
            double q = bandwidthOrQOrS;
            double a = pow(10, dbGain / 40);
            s = 1.0 / ((1.0 / (q * q) - 2.0) / (a + 1.0 / a) + 1.0);
        }
        double centerFreqFactor = pow(10.0, abs(dbGain) / 80.0 / s);
        if (type == BiQuad::LOW_SHELF)
            biquadFreq *= centerFreqFactor;
        else
            biquadFreq /= centerFreqFactor;
    }

    // A single master BiQuad performs the coefficient calculation shared by
    // all channels.
    BiQuad masterBiquad(type, dbGain, biquadFreq, sampleRate, bandwidthOrQOrS, isBandwidthOrS);

    double temp_a0;
    double temp_coeffs[4];

    masterBiquad.getCoefficients(temp_coeffs, temp_a0);

    // Populate the SoA vectors with the coefficients for all channels.
    // The mapping is based on the original `process` function's variable usage:
    // result = a0*sample + a[1]*x2 + a[0]*x1 - a[3]*y2 - a[2]*y1;
    // Textbook: result = b0*sample + b2*x2 + b1*x1 - a2*y2 - a1*y1
    const double coeff_b1 = temp_coeffs[0];
    const double coeff_b2 = temp_coeffs[1];
    const double coeff_a1 = temp_coeffs[2];
    const double coeff_a2 = temp_coeffs[3];

    for (unsigned i = 0; i < channelCount; ++i)
    {
        this->a0[i] = temp_a0;      // Corresponds to b0
        this->b1[i] = coeff_b1;     // Corresponds to b1
        this->b2[i] = coeff_b2;     // Corresponds to b2
        this->a1[i] = coeff_a1;     // Corresponds to a1
        this->a2[i] = coeff_a2;     // Corresponds to a2
    }

    return channelNames;
}

#pragma AVRT_CODE_BEGIN

void BiQuadFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("BiQuadFilter::process");
	// FTZ/DAZ is enabled once at the engine boundary by MxcsrFtzDazGuard;
	// individual filters do not touch MXCSR.

    // Kernel split is decided by planBiQuadKernels; see BiQuadKernelPlan.h for
    // the contract. `width` is 8/4/2 on the x86 targets and 2 on ARM64 NEON.
    const hn::ScalableTag<double> d;
    const unsigned width = (unsigned)hn::Lanes(d);
    const BiQuadKernelPlan plan = planBiQuadKernels((unsigned)channelCount, width);

    if (plan.simdChannels > 0)
        process_simd(output, input, frameCount, 0, plan.simdChannels);

    for (unsigned c = plan.simdChannels; c < plan.simdChannels + plan.pairedChannels; c += 2)
        process_dual(output, input, frameCount, c);

    if (plan.singleChannels > 0)
        process_scalar(output, input, frameCount, plan.simdChannels + plan.pairedChannels);
}


// Processes Lanes(d) channels at a time. The biquad is stored as SoA across
// channels, so the per-frame input samples for the active channel group live in
// separate pointers (input[i+k][j]); Highway has no portable pointer-array
// gather, so we marshal them through a small stack buffer and likewise scatter
// the result. The FMA op order of the coefficient/state math is deliberate:
// reordering it changes per-target output (the SSE2 build, which has no FMA
// hardware, gets the same mul+add Highway emits).
void BiQuadFilter::process_simd(double** output, double** input, unsigned frameCount, unsigned startChannel, unsigned numChannels)
{
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);

    for (unsigned i = startChannel; i < startChannel + numChannels; i += (unsigned)N)
    {
        const auto _a0 = hn::LoadU(d, &a0[i]);
        const auto _b1 = hn::LoadU(d, &b1[i]);
        const auto _b2 = hn::LoadU(d, &b2[i]);
        const auto _a1 = hn::LoadU(d, &a1[i]);
        const auto _a2 = hn::LoadU(d, &a2[i]);

        auto _x1 = hn::LoadU(d, &x1[i]);
        auto _x2 = hn::LoadU(d, &x2[i]);
        auto _y1 = hn::LoadU(d, &y1[i]);
        auto _y2 = hn::LoadU(d, &y2[i]);

        alignas(64) double gather[hn::MaxLanes(d)];
        alignas(64) double scatter[hn::MaxLanes(d)];

        for (unsigned j = 0; j < frameCount; ++j)
        {
            for (size_t k = 0; k < N; ++k)
                gather[k] = input[i + k][j];
            const auto _sample = hn::LoadU(d, gather);

            // result = a0*sample + b1*x1 + b2*x2 - a1*y1 - a2*y2
            auto result = hn::Mul(_a0, _sample);
            result = hn::MulAdd(_b1, _x1, result);
            result = hn::MulAdd(_b2, _x2, result);
            result = hn::NegMulAdd(_a1, _y1, result);
            result = hn::NegMulAdd(_a2, _y2, result);

            _x2 = _x1; _x1 = _sample;
            _y2 = _y1; _y1 = result;

            hn::StoreU(result, d, scatter);
            for (size_t k = 0; k < N; ++k)
                output[i + k][j] = scatter[k];
        }

        hn::StoreU(_x1, d, &x1[i]);
        hn::StoreU(_x2, d, &x2[i]);
        hn::StoreU(_y1, d, &y1[i]);
        hn::StoreU(_y2, d, &y2[i]);
    }
}


// Two leftover channels at a time: both loop-carried dependency chains live in
// registers and overlap in the pipeline, which roughly halves the
// latency-bound cost of running them back to back. The per-channel expression
// is textually identical to process_scalar, so this kernel produces the same
// bits as the single-chain kernel on every target; only the scheduling
// differs. BiQuadKernelTests pins that equality.
void BiQuadFilter::process_dual(double** output, double** input, unsigned frameCount, unsigned startChannel)
{
    const unsigned chA = startChannel;
    const unsigned chB = startChannel + 1;

    double a_x1 = x1[chA], a_x2 = x2[chA], a_y1 = y1[chA], a_y2 = y2[chA];
    double b_x1v = x1[chB], b_x2v = x2[chB], b_y1 = y1[chB], b_y2 = y2[chB];
    const double a_c0 = a0[chA], a_cb1 = b1[chA], a_cb2 = b2[chA], a_ca1 = a1[chA], a_ca2 = a2[chA];
    const double b_c0 = a0[chB], b_cb1 = b1[chB], b_cb2 = b2[chB], b_ca1 = a1[chB], b_ca2 = a2[chB];
    const double* inputA = input[chA];
    const double* inputB = input[chB];
    double* outputA = output[chA];
    double* outputB = output[chB];

    for (unsigned j = 0; j < frameCount; ++j)
    {
        double sampleA = inputA[j];
        double sampleB = inputB[j];
        double resultA = a_c0 * sampleA + a_cb1 * a_x1 + a_cb2 * a_x2 - a_ca1 * a_y1 - a_ca2 * a_y2;
        double resultB = b_c0 * sampleB + b_cb1 * b_x1v + b_cb2 * b_x2v - b_ca1 * b_y1 - b_ca2 * b_y2;
        a_x2 = a_x1;
        a_x1 = sampleA;
        b_x2v = b_x1v;
        b_x1v = sampleB;
        a_y2 = a_y1;
        a_y1 = resultA;
        b_y2 = b_y1;
        b_y1 = resultB;
        outputA[j] = resultA;
        outputB[j] = resultB;
    }

    x1[chA] = a_x1; x2[chA] = a_x2; y1[chA] = a_y1; y2[chA] = a_y2;
    x1[chB] = b_x1v; x2[chB] = b_x2v; y1[chB] = b_y1; y2[chB] = b_y2;
}


// Scalar processing for any final leftover channels (e.g., the 7th channel in a 7.1 setup)
void BiQuadFilter::process_scalar(double** output, double** input, unsigned frameCount, unsigned startChannel)
{
    for (unsigned i = startChannel; i < channelCount; ++i)
    {
        double cur_x1 = x1[i], cur_x2 = x2[i];
        double cur_y1 = y1[i], cur_y2 = y2[i];
        const double c_a0 = a0[i], c_b1 = b1[i], c_b2 = b2[i];
        const double c_a1 = a1[i], c_a2 = a2[i];
        const double* inputChannel = input[i];
        double* outputChannel = output[i];
        for (unsigned j = 0; j < frameCount; ++j)
        {
            double sample = inputChannel[j];
            double result = c_a0 * sample + c_b1 * cur_x1 + c_b2 * cur_x2 - c_a1 * cur_y1 - c_a2 * cur_y2;
            cur_x2 = cur_x1;
            cur_x1 = sample;
            cur_y2 = cur_y1;
            cur_y1 = result;
            outputChannel[j] = result;
        }
        x1[i] = cur_x1; x2[i] = cur_x2;
        y1[i] = cur_y1; y2[i] = cur_y2;
    }
}
#pragma AVRT_CODE_END
