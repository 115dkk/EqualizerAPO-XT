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
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <utility>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fftw3.h>

#include "ConvolverMuteDiagnostics.h"
#include "services/logging/Logging.h"
#include "runtime/memory/AlignedMemory.h"
#include "runtime/concurrency/ParallelExecutor.h"
#include "ConvolutionFilter.h"
#include "diagnostics/performance/PerfProfile.h"

using std::vector;
using std::wstring;

// The IR intake (libsndfile read, hardening, deinterleave) and the weak-ptr
// cache live in IrCache.cpp, shared with MultiConvolutionFilter.

namespace
{
	// Audit #250 A4: the shared bookkeeping; see ConvolverMuteDiagnostics.h.
	ConvolverMuteDiagnostics muteDiagnostics;
}

ConvolutionFilter::ConvolutionFilter(const wstring& filename)
{
	this->filename = filename;
	filterFrameCount = 0;
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
	// audio 콜백 중 재초기화는 파일 I/O, FFTW plan, malloc/free를 일으키므로 금지한다.
	// mismatch가 들어오면 무음으로 빠지고, 진단은 원자 카운터에만 남긴다. 정상
	// stream에서는 LockForProcess가 frameCount를 고정하므로 이 분기는 거의 들어오지 않는다.
	if (frameCount != filterFrameCount)
	{
		// No logging here. Logging opens, writes and closes %TEMP%\EqualizerAPO.log
		// for every line, and this branch fires exactly when the stream can least
		// afford blocking I/O on the audio thread (a format change, a device switch).
		// cleanup() formats and writes the same information.
		muteDiagnostics.recordMute(frameCount, frameCountMismatchLogged);
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
	// Deferred report of the mute path that process() took on the audio thread.
	// It runs before the members are cleared, so the initialized block size is
	// still available, and only for instances that actually muted.
	if (frameCountMismatchLogged)
		LogF(kConvolverMuteReportFormat, kFrameCountMismatchLogPrefix,
			muteDiagnostics.firstMuteFrameCount.load(std::memory_order_relaxed), filterFrameCount,
			muteDiagnostics.muteCallCount.load(std::memory_order_relaxed));

	// HConvSingleArray::reset() runs the exact close-then-free sequence; assigning
	// nullptr makes the teardown automatic and idempotent.
	filters = nullptr;
	// Release this filter's hold on the cached IR. With the cache holding only weak
	// references, dropping the last shared_ptr frees the entry.
	irEntry.reset();
	filterFrameCount = 0;
	frameCountMismatchLogged = false;
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

	// Build one immutable frequency-domain filter bank per distinct IR channel.
	// Output channels that reuse a mono/stereo IR still receive independent
	// histories, mix buffers and FFTW execution plans.
	const unsigned distinctIrChannels = (std::min)(channelCount, ir->channels);
	std::vector<ConvolverUnitSource> sources(channelCount);
	for (unsigned i = 0; i < channelCount; ++i)
	{
		const unsigned irChannel = i % ir->channels;
		sources[i] = { ir->buffers[irChannel].data(), ir->frames,
			i < distinctIrChannels ? i : irChannel };
	}
	filters = buildConvolverArray(sources, frameCount);
}
