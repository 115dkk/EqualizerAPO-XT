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
#include <cstring>
#include <utility>

#include "services/logging/Logging.h"
#include "ConvolutionFilter.h"
#include "ConvolverProcess.h"
#include "diagnostics/performance/PerfProfile.h"

using std::vector;
using std::wstring;

// The IR intake (libsndfile read, hardening, deinterleave) and the weak-ptr
// cache live in IrCache.cpp, shared with MultiConvolutionFilter.

namespace
{
	// Audit #250 A4: the shared bookkeeping; see ConvolverMuteDiagnostics.h.
	ConvolverMuteDiagnostics convolutionMuteDiagnostics;
}

ConvolutionFilter::ConvolutionFilter(JudgedPath filename, bool ignoreTrailingSilence, bool parallelConvolution)
	: ConvolutionFilter(std::move(filename), convolutionMuteDiagnostics,
		kFrameCountMismatchLogPrefix, ignoreTrailingSilence, parallelConvolution)
{
}

ConvolutionFilter::ConvolutionFilter(JudgedPath filename, ConvolverMuteDiagnostics& muteDiagnostics,
	const wchar_t* muteLogPrefix, bool ignoreTrailingSilence, bool parallelConvolution)
	: bank(muteDiagnostics, muteLogPrefix), filename(std::move(filename)),
	  ignoreTrailingSilence(ignoreTrailingSilence), parallelConvolution(parallelConvolution)
{
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

	initializeFilters(maxFrameCount);

	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void ConvolutionFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("ConvolutionFilter::process");
	if (!bank.installed())
		return;
	if (frameCount == 0)
		return;

	// libHybridConv fixes framelength at hcInitPartitioned. Reinitializing in
	// an audio callback would perform file I/O, FFTW planning and malloc/free.
	// Mismatches mute output and record diagnostics only in atomic counters.
	// Normally LockForProcess fixes frameCount, so this branch is rarely taken.
	if (!bank.admit(frameCount))
	{
		// admit() recorded the mute; the deferred report is written by
		// cleanup() through bank.finishAndReport(), nothing is logged on the
		// audio thread.
		for (unsigned i = 0; i < channelCount; i++)
			memset(output[i], 0, sizeof(double) * frameCount);
		return;
	}

	ConvolverProcessJob job{bank, input, output};
	processConvolverUnits(job, channelCount, parallelWork);
}
#pragma AVRT_CODE_END

void ConvolutionFilter::cleanup()
{
	fanOutLease = {};
	parallelWork = false;
	// Deferred report of the mute path that process() took on the audio
	// thread, then disarm and release the units (close-then-free), in one call.
	bank.finishAndReport();
	// Release this filter's hold on the cached IR. With the cache holding only weak
	// references, dropping the last shared_ptr frees the entry.
	irEntry.reset();
}

void ConvolutionFilter::initializeFilters(unsigned frameCount)
{
	const wstring displayPath = filename.path();
	auto ir = loadIrCached(filename, sampleRate, ignoreTrailingSilence);
	filename = JudgedPath{};
	if (!ir)
		return;

	// Pin the cached IR for this filter's lifetime. The process-wide cache keeps
	// only a weak reference, so this member is what keeps the entry resident while
	// the filter exists; cleanup() releases it.
	irEntry = ir;

	TraceF(L"Convolving using impulse response file %s (%u channels, %u frames)",
		displayPath.c_str(), ir->channels, ir->frames);

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
	bank.install(buildConvolverArray(sources, frameCount), frameCount);
	if (bank.installed() && parallelConvolution && channelCount >= 2)
	{
		fanOutLease = eapo::rt::FanOut::Lease::acquire();
		parallelWork = useConvolverFanOut(channelCount, ir->frames, *bank.unit(0));
	}
}
