/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <sndfile.h>
#include <fftw3.h>

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "ConvolutionFilter.h"
#include "helpers/PerfProfile.h"

using std::abs;
using std::vector;
using std::wstring;

ConvolutionFilter::ConvolutionFilter(wstring filename)
{
	this->filename = filename;
	filters = nullptr;
	filterFrameCount = 0;
}

ConvolutionFilter::~ConvolutionFilter()
{
	cleanup();
}

vector<wstring> ConvolutionFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	cleanup();

	this->sampleRate = sampleRate;
	this->maxFrameCount = maxFrameCount;
	channelCount = (unsigned)channelNames.size();
	filterFrameCount = 0;

	initializeFilters(maxFrameCount);
	if (filters != nullptr)
		filterFrameCount = maxFrameCount;

	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void ConvolutionFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("ConvolutionFilter::process");
	if (filters == nullptr)
		return;
	if (frameCount == 0)
		return;

	if (frameCount != filterFrameCount)
	{
		cleanup();
		initializeFilters(frameCount);
		if (filters == nullptr)
			return;
		filterFrameCount = frameCount;
	}

	for (unsigned i = 0; i < channelCount; i++)
	{
		double* inputChannel = input[i];
		double* outputChannel = output[i];
		HConvSingle* filter = &filters[i];

		hcPutSingle(filter, inputChannel);
		hcProcessSingle(filter);
		hcGetSingle(filter, outputChannel);
	}
}
#pragma AVRT_CODE_END

void ConvolutionFilter::cleanup()
{
	if (filters != nullptr)
	{
		for (unsigned i = 0; i < channelCount; i++)
			hcCloseSingle(&filters[i]);

		MemoryHelper::free(filters);
		filters = nullptr;
	}
	filterFrameCount = 0;
}

void ConvolutionFilter::initializeFilters(unsigned frameCount)
{
	SF_INFO info;

	SNDFILE* inFile = sf_wchar_open(filename.c_str(), SFM_READ, &info);
	if (inFile == nullptr)
	{
		LogF(L"Error while reading impulse response file: %S", sf_strerror(inFile));
	}
	else if (abs(sampleRate - info.samplerate) > 1.0)
	{
		LogF(L"Impulse response sample rate (%d Hz) does not match device sample rate (%f Hz)", info.samplerate, sampleRate);
	}
	else
	{
		TraceF(L"Convolving using impulse response file %s", filename.c_str());
		unsigned fileChannelCount = info.channels;
		unsigned fileFrameCount = (unsigned)info.frames;

		double* interleavedBuf = new double[fileFrameCount * fileChannelCount];

		sf_count_t numRead = 0;
		while (numRead < fileFrameCount)
			numRead += sf_readf_double(inFile, interleavedBuf + numRead * fileChannelCount, fileFrameCount - numRead);

		sf_close(inFile);
		inFile = nullptr;

		double** bufs = new double* [fileChannelCount];
		for (unsigned i = 0; i < fileChannelCount; i++)
		{
			double* buf = new double[fileFrameCount];
			double* p = interleavedBuf + i;
			for (unsigned j = 0; j < fileFrameCount; j++)
			{
				buf[j] = p[j * fileChannelCount];
			}

			bufs[i] = buf;
		}

		fftw_make_planner_thread_safe();
		filters = (HConvSingle*)MemoryHelper::alloc(sizeof(HConvSingle) * channelCount);
		for (unsigned i = 0; i < channelCount; i++)
		{
			hcInitSingle(&filters[i], bufs[i % fileChannelCount], fileFrameCount, frameCount, 1);
		}

		for (unsigned i = 0; i < fileChannelCount; i++)
		{
			delete[] bufs[i];
		}
		delete[] bufs;
		delete[] interleavedBuf;
	}
}
