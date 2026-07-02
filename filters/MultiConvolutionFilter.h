/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

#pragma once

#include <string>
#include <vector>

#include "IFilter.h"
#include "libHybridConv-0.1.1/libHybridConv_eapo.h"

// Multi-input synthesis convolution ("다중 합성 컨볼루션"). Unlike the 1:1
// ConvolutionFilter, this filter reads every selected input channel, convolves
// each with the corresponding channel of a single multi-channel impulse
// response, sums the results, and writes them to one output channel. It is the
// building block for BRIR/crossfeed setups where one ear's output is the sum of
// several speaker signals, each passed through its own impulse response.
//
// getInPlace() is false because the output channel differs from the inputs, so
// the engine hands us separate input/output buffers.
#pragma AVRT_VTABLES_BEGIN
class MultiConvolutionFilter : public IFilter
{
public:
	MultiConvolutionFilter(const std::wstring& outputChannel, const std::wstring& filename);
	virtual ~MultiConvolutionFilter();
	bool getInPlace() override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	void cleanup();

	std::wstring outputChannel;
	std::wstring filename;
	float sampleRate;
	unsigned inputChannelCount;
	unsigned filterFrameCount;
	// One convolution state per input channel; filters[i] convolves input i with
	// impulse-response channel (i modulo IR channel count).
	HConvSingle* filters;
	// Per-input scratch buffer for one channel's convolution result before it is
	// summed into the single output. Sized in initialize() so process() never
	// allocates on the audio thread.
	std::vector<std::vector<double>> tempBuffers;
};
#pragma AVRT_VTABLES_END
