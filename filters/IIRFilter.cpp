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
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include "runtime/memory/MemoryHelper.h"
#include "IIRFilter.h"
#include "diagnostics/performance/PerfProfile.h"

using std::abs;
using std::vector;
using std::wstring;

#define IS_DENORMAL(d) (abs(d) < DBL_MIN)

IIRFilter::IIRFilter(const vector<double>& coefficients)
{
	if (coefficients.size() < 4 || coefficients.size() % 2 != 0)
		throw std::invalid_argument("IIR coefficients must contain matching b and a terms");
	for (double coefficient : coefficients)
		if (!std::isfinite(coefficient))
			throw std::invalid_argument("IIR coefficients must be finite");

	order = static_cast<unsigned>(coefficients.size() / 2 - 1);
	a.resize(order);
	b.resize(order);

	double a0 = coefficients[order + 1];
	if (a0 == 0.0)
		throw std::invalid_argument("IIR a0 coefficient must not be zero");
	b0 = coefficients[0] / a0;
	for (unsigned i = 0; i < order; i++)
	{
		b[i] = coefficients[i + 1] / a0;
		a[i] = -coefficients[i + order + 2] / a0;
	}
}

IIRFilter::~IIRFilter()
{
}

vector<wstring> IIRFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	channelCount = (unsigned)channelNames.size();

	// The unsigned multiplications could wrap before widening to size_t
	// (CodeQL cpp/integer-multiplication-cast-to-long); validate in size_t and
	// use the same element count for allocation and initialization. Checked
	// before freeing the old buffers so a failure cannot leave x/y dangling.
	const size_t maxSize = (std::numeric_limits<size_t>::max)();
	if (channelCount != 0 && static_cast<size_t>(order) > maxSize / channelCount)
		throw std::bad_alloc();

	const size_t stateCount = static_cast<size_t>(order) * channelCount;
	if (stateCount > maxSize / sizeof(double))
		throw std::bad_alloc();

	vector<double> newX(stateCount, 0.0);
	vector<double> newY(stateCount, 0.0);
	x.swap(newX);
	y.swap(newY);

	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void IIRFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("IIRFilter::process");
	for (unsigned i = 0; i < channelCount; i++)
	{
		const double* inputChannel = input[i];
		double* outputChannel = output[i];

		unsigned channelOffset = i * order;
		double* xo = x.data() + channelOffset;
		double* yo = y.data() + channelOffset;
		for (unsigned j = 0; j < frameCount; j++)
		{
			double sample = inputChannel[j];
			double sum = b0 * sample;

			for (unsigned k = order - 1; k > 0; k--)
			{
				sum += b[k] * xo[k];
				xo[k] = xo[k - 1];
			}

			sum += b[0] * xo[0];

			for (unsigned k = order - 1; k > 0; k--)
			{
				sum += a[k] * yo[k];
				yo[k] = yo[k - 1];
			}

			sum += a[0] * yo[0];

			xo[0] = sample;
			yo[0] = sum;

			outputChannel[j] = static_cast<double>(sum);
		}
	}

	for (unsigned i = 0; i < channelCount * order; i++)
	{
		if (IS_DENORMAL(x[i]))
			x[i] = 0.0;
		if (IS_DENORMAL(y[i]))
			y[i] = 0.0;
	}
}
#pragma AVRT_CODE_END
