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
#include <atomic>
#include <cmath>
#include <memory>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fftw3.h>

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "ConvolutionFilter.h"
#include "helpers/PerfProfile.h"

using std::vector;
using std::wstring;

// The IR intake (libsndfile read, hardening, deinterleave) and the weak-ptr
// cache live in IrCache.cpp, shared with MultiConvolutionFilter. (audit #146 TD001)

namespace
{
	// Running total of process() calls that took the frameCount-mismatch mute path,
	// across all ConvolutionFilter instances. The mute is otherwise silent after the
	// first log, so this makes the condition observable (logged in cleanup()). The
	// mute path can run on the audio thread, hence atomic.
	std::atomic<unsigned long long> muteCallCount{ 0 };
}

ConvolutionFilter::ConvolutionFilter(const wstring& filename)
{
	this->filename = filename;
	filterFrameCount = 0;
	maxFrameCount = 0;
	frameCountMismatchLogged = false;
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

	// libHybridConv는 hcInitSingle 시점의 framelength로 고정 처리한다.
	// 과거에는 process() 안에서 cleanup() + initializeFilters()를 호출해 audio 콜백 중에 파일 I/O,
	// FFTW plan, malloc/free가 발생했다. RT 안정성을 위해 재초기화를 금지하고, mismatch가 들어오면
	// 한 번만 로그를 남긴 뒤 무음으로 빠진다. 정상 stream에서는 LockForProcess가 frameCount를 고정하므로
	// 이 분기는 거의 들어오지 않는다.
	if (frameCount != filterFrameCount)
	{
		const unsigned long long count = muteCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (!frameCountMismatchLogged)
		{
			LogF(L"ConvolutionFilter: frameCount %u differs from initialized %u; output muted (audio-thread re-init skipped) [mute calls so far: %llu]", frameCount, filterFrameCount, count);
			frameCountMismatchLogged = true;
		}
		for (unsigned i = 0; i < channelCount; i++)
			memset(output[i], 0, sizeof(double) * frameCount);
		return;
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
	// HConvSingleArray::reset() runs the exact close-then-free sequence; assigning
	// nullptr makes the teardown automatic and idempotent.
	filters = nullptr;
	// Release this filter's hold on the cached IR. With the cache holding only weak
	// references, dropping the last shared_ptr frees the entry.
	irEntry.reset();
	filterFrameCount = 0;
	frameCountMismatchLogged = false;

	// Surface how often the mismatch mute fired (silent after the first log).
	if (muteCallCount.load(std::memory_order_relaxed) != 0)
		LogF(L"ConvolutionFilter: frameCount-mismatch mute fired %llu time(s) total", muteCallCount.load(std::memory_order_relaxed));
}

void ConvolutionFilter::initializeFilters(unsigned frameCount)
{
	auto ir = loadIrCached(filename, sampleRate);
	if (!ir)
		return;

	// Pin the cached IR for this filter's lifetime. The process-wide cache keeps
	// only a weak reference, so this member is what keeps the entry resident while
	// the filter exists; cleanup() releases it.
	irEntry = ir;

	TraceF(L"Convolving using impulse response file %s (%u channels, %u frames)",
		filename.c_str(), ir->channels, ir->frames);

	fftw_make_planner_thread_safe();
	HConvSingle* allocated = (HConvSingle*)MemoryHelper::alloc(sizeof(HConvSingle) * channelCount);
	if (allocated == nullptr)
	{
		// alloc returns nullptr on failure; bail to the inert state (filters stays
		// null from cleanup(), process() then no-ops) rather than dereferencing it.
		LogF(L"ConvolutionFilter: could not allocate %u filter slots", channelCount);
		return;
	}
	filters.adopt(allocated, channelCount);
	for (unsigned i = 0; i < channelCount; i++)
	{
		// hcInitSingle reads the IR samples during planning but does not retain
		// the pointer, so it is safe to feed it the shared cache buffer.
		const std::vector<double>& src = ir->buffers[i % ir->channels];
		hcInitSingle(&filters[i], const_cast<double*>(src.data()), static_cast<int>(ir->frames), static_cast<int>(frameCount), 1);
	}
}
