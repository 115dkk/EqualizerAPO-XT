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

#include "stdafx.h"
#include <cmath>
#include <climits>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <sndfile.h>
#include <fftw3.h>

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "MultiConvolutionFilter.h"

using std::abs;
using std::vector;
using std::wstring;

MultiConvolutionFilter::MultiConvolutionFilter(const wstring& outputChannel, const wstring& filename)
{
	this->outputChannel = outputChannel;
	this->filename = filename;
	sampleRate = 0.0f;
	inputChannelCount = 0;
	filterFrameCount = 0;
	filters = nullptr;
}

MultiConvolutionFilter::~MultiConvolutionFilter()
{
	cleanup();
}

vector<wstring> MultiConvolutionFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	cleanup();

	this->sampleRate = sampleRate;
	inputChannelCount = (unsigned)channelNames.size();
	filterFrameCount = 0;

	// The output channel is declared regardless of whether the IR loads, so a
	// bad path degrades to a silent output channel instead of vanishing (which
	// would shift every following channel selection).
	vector<wstring> outChannelNames = {outputChannel};

	if (inputChannelCount == 0)
		return outChannelNames;

	SF_INFO info{};
	SNDFILE* in = sf_wchar_open(filename.c_str(), SFM_READ, &info);
	if (in == nullptr)
	{
		LogFStatic(L"Error while reading impulse response file: %S", sf_strerror(in));
		return outChannelNames;
	}
	if (abs(sampleRate - info.samplerate) > 1.0)
	{
		LogFStatic(L"Impulse response sample rate (%d Hz) does not match device sample rate (%f Hz)", info.samplerate, sampleRate);
		sf_close(in);
		return outChannelNames;
	}
	if (info.frames <= 0 || info.channels <= 0 || info.frames > INT_MAX)
	{
		LogFStatic(L"Impulse response has no usable audio (frames=%lld, channels=%d); ignoring %s",
			static_cast<long long>(info.frames), info.channels, filename.c_str());
		sf_close(in);
		return outChannelNames;
	}

	const unsigned irChannels = (unsigned)info.channels;
	const unsigned irFrames = (unsigned)info.frames;
	vector<double> interleaved((size_t)irFrames * irChannels);
	sf_count_t numRead = 0;
	while (numRead < info.frames)
	{
		sf_count_t got = sf_readf_double(in, interleaved.data() + numRead * irChannels, info.frames - numRead);
		if (got <= 0)
			break;
		numRead += got;
	}
	sf_close(in);

	// Deinterleave into channel-major IR buffers so each convolution reads a
	// contiguous per-channel impulse response.
	vector<vector<double>> irBuffers(irChannels, vector<double>(irFrames));
	for (unsigned c = 0; c < irChannels; c++)
		for (unsigned f = 0; f < irFrames; f++)
			irBuffers[c][f] = interleaved[(size_t)f * irChannels + c];

	fftw_make_planner_thread_safe();
	filters = (HConvSingle*)MemoryHelper::alloc(sizeof(HConvSingle) * inputChannelCount);
	if (filters == nullptr)
	{
		LogF(L"MultiConvolutionFilter: could not allocate %u filter slots", inputChannelCount);
		return outChannelNames;
	}

	tempBuffers.assign(inputChannelCount, vector<double>(maxFrameCount, 0.0));
	for (unsigned i = 0; i < inputChannelCount; i++)
	{
		// Input i pairs with IR channel (i mod irChannels): an exact match when
		// the IR channel count equals the selected input count, and a sensible
		// wrap (e.g. a mono IR applied to every input) otherwise.
		vector<double>& src = irBuffers[i % irChannels];
		hcInitSingle(&filters[i], src.data(), (int)irFrames, (int)maxFrameCount, 1);
	}
	filterFrameCount = maxFrameCount;

	return outChannelNames;
}

#pragma AVRT_CODE_BEGIN
void MultiConvolutionFilter::process(double** output, double** input, unsigned frameCount)
{
	if (frameCount == 0)
		return;

	// No usable IR (bad path / sample-rate mismatch): emit silence on the output
	// channel rather than leaving it uninitialized.
	if (filters == nullptr)
	{
		memset(output[0], 0, sizeof(double) * frameCount);
		return;
	}

	// libHybridConv fixes its block length at hcInitSingle time. A frameCount that
	// differs from the initialized value cannot be fed to hcProcessSingle, so mute
	// this block instead (matches ConvolutionFilter's real-time-safe behaviour).
	if (frameCount != filterFrameCount)
	{
		memset(output[0], 0, sizeof(double) * frameCount);
		return;
	}

	// Convolve each selected input with its paired IR channel and sum the results
	// into the single output channel.
	memset(output[0], 0, sizeof(double) * frameCount);
	for (unsigned i = 0; i < inputChannelCount; i++)
	{
		hcPutSingle(&filters[i], input[i]);
		hcProcessSingle(&filters[i]);
		hcGetSingle(&filters[i], tempBuffers[i].data());
		for (unsigned f = 0; f < frameCount; f++)
			output[0][f] += tempBuffers[i][f];
	}
}
#pragma AVRT_CODE_END

void MultiConvolutionFilter::cleanup()
{
	if (filters != nullptr)
	{
		for (unsigned i = 0; i < inputChannelCount; i++)
			hcCloseSingle(&filters[i]);
		MemoryHelper::free(filters);
		filters = nullptr;
	}
	tempBuffers.clear();
	filterFrameCount = 0;
}
