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
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fftw3.h>

#include "audio/ChannelHelper.h"
#include "services/logging/LogHelper.h"
#include "runtime/memory/MemoryHelper.h"
#include "runtime/concurrency/ParallelExecutor.h"
#include "diagnostics/performance/PerfProfile.h"
#include "ConvolverMuteDiagnostics.h"
#include "MultiConvolutionFilter.h"

using std::find;
using std::vector;
using std::wstring;

namespace
{
	// Audit #250 A4: the shared bookkeeping; see ConvolverMuteDiagnostics.h.
	// Per-class instance, like ConvolutionFilter's and HilbertFilter's.
	ConvolverMuteDiagnostics muteDiagnostics;
}

MultiConvolutionFilter::MultiConvolutionFilter(const vector<MultiConvolutionCommand::Mapping>& mappings, const wstring& filename)
{
	this->mappings = mappings;
	this->filename = filename;
	sampleRate = 0.0f;
	filterFrameCount = 0;
	frameCountMismatchLogged = false;
	unitCount = 0;
}

MultiConvolutionFilter::~MultiConvolutionFilter()
{
	cleanup();
}

vector<wstring> MultiConvolutionFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	cleanup();

	this->sampleRate = sampleRate;
	filterFrameCount = 0;

	// Resolve every mapping's output slot and input channel first: the targets
	// are declared regardless of whether the IR loads, so a bad path degrades to
	// silent output channels instead of vanishing (which would shift every
	// following channel selection). Targets resolve like Copy's (name, alias or
	// 1-based number, or a new virtual channel); duplicate targets share one
	// slot and the later mapping overwrites the earlier one in process().
	vector<wstring> outChannelNames;
	plans.assign(mappings.size(), MappingPlan{0, -1, 0, 0});
	for (size_t i = 0; i < mappings.size(); i++)
	{
		wstring channelName = mappings[i].targetChannel;
		int channelIndex = ChannelHelper::getChannelIndex(channelName, channelNames, true);
		if (channelIndex != -1)
			channelName = channelNames[channelIndex];

		vector<wstring>::const_iterator it = find(outChannelNames.begin(), outChannelNames.end(), channelName);
		plans[i].outputSlot = (unsigned)(it - outChannelNames.begin());
		if (it == outChannelNames.end())
			outChannelNames.push_back(channelName);

		// The mapping convolves its target's own pre-command signal; a target
		// that does not exist yet reads silence.
		plans[i].inputChannel = channelIndex;
	}

	// Shared IR intake + cache (IrCache.cpp): validates the file, deinterleaves
	// to channel-major buffers, and lets a config reload (or another filter on
	// the same IR) skip the disk read entirely.
	auto ir = loadIrCached(filename, sampleRate);
	if (!ir)
		return outChannelNames;
	irEntry = ir;

	const unsigned irChannels = ir->channels;
	const unsigned irFrames = ir->frames;

	// Expand each mapping to its participating IR channels. The simple form
	// (empty list) means every channel of the file at unity; explicit
	// references beyond the file's channel count are dropped with a log line,
	// so a wrong index contributes silence instead of failing the whole line.
	// dB factors are converted to linear scales here, like Copy does at
	// assignment-build time.
	vector<vector<MultiConvolutionCommand::IrChannelRef>> perMapping(mappings.size());
	unsigned totalUnits = 0;
	for (size_t i = 0; i < mappings.size(); i++)
	{
		if (mappings[i].irChannels.empty())
		{
			perMapping[i].resize(irChannels);
			for (unsigned c = 0; c < irChannels; c++)
				perMapping[i][c] = MultiConvolutionCommand::IrChannelRef(c);
		}
		else
		{
			for (const MultiConvolutionCommand::IrChannelRef& ref : mappings[i].irChannels)
			{
				if (ref.channel >= irChannels)
				{
					LogFStatic(L"Impulse response channel %u out of range (file has %u channels): %s", ref.channel, irChannels, filename.c_str());
					continue;
				}
				perMapping[i].push_back(ref);
			}
		}
		totalUnits += (unsigned)perMapping[i].size();
	}

	if (totalUnits == 0)
		return outChannelNames;

	tempBuffer.assign(maxFrameCount, 0.0);
	unitFactors.assign(totalUnits, 1.0);
	std::vector<unsigned> unitChannels(totalUnits);
	unsigned next = 0;
	for (size_t i = 0; i < mappings.size(); i++)
	{
		plans[i].firstUnit = next;
		for (const MultiConvolutionCommand::IrChannelRef& ref : perMapping[i])
		{
			unitChannels[next] = ref.channel;
			unitFactors[next] = ref.isDecibel ? pow(10.0, ref.factor / 20.0) : ref.factor;
			next++;
		}
		plans[i].unitCount = next - plans[i].firstUnit;
	}
	// A routing expression may reference the same IR channel more than once.
	// Transform each referenced channel once and share only its immutable bank.
	const unsigned noPrototype = (std::numeric_limits<unsigned>::max)();
	std::vector<unsigned> prototypes(ir->channels, noPrototype);
	for (unsigned unit = 0; unit < totalUnits; ++unit)
	{
		const unsigned irChannel = unitChannels[unit];
		if (prototypes[irChannel] == noPrototype)
			prototypes[irChannel] = unit;
	}
	std::vector<ConvolverUnitSource> sources(totalUnits);
	for (unsigned unit = 0; unit < totalUnits; ++unit)
	{
		const unsigned irChannel = unitChannels[unit];
		sources[unit] = { ir->buffers[irChannel].data(), irFrames, prototypes[irChannel] };
	}
	filters = buildConvolverArray(sources, maxFrameCount);
	if (filters == nullptr)
		return outChannelNames;
	unitCount = next;
	filterFrameCount = maxFrameCount;

	return outChannelNames;
}

#pragma AVRT_CODE_BEGIN
void MultiConvolutionFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("MultiConvolutionFilter::process");
	if (frameCount == 0)
		return;

	if (filters != nullptr && frameCount != filterFrameCount)
	{
		// No logging here. LogHelper opens, writes and closes %TEMP%\EqualizerAPO.log
		// for every line, and this branch fires exactly when the stream can least
		// afford blocking I/O on the audio thread. cleanup() writes the same line.
		muteDiagnostics.recordMute(frameCount, frameCountMismatchLogged);
	}

	// libHybridConv fixes its block length at hcInitSingle time, so a block of
	// any other size cannot be fed to the convolver; without a usable IR there
	// is nothing to feed at all. Either way every mapping target still gets
	// written (silence), never left uninitialized.
	const bool usable = filters != nullptr && frameCount == filterFrameCount;

	for (const MappingPlan& plan : plans)
	{
		double* out = output[plan.outputSlot];
		// Zeroing per mapping (in order) makes a duplicate target behave like
		// Copy: the later mapping overwrites the earlier one.
		memset(out, 0, sizeof(double) * frameCount);
		if (!usable || plan.inputChannel < 0 || plan.unitCount == 0)
			continue;

		double* in = input[plan.inputChannel];
		for (unsigned u = plan.firstUnit; u < plan.firstUnit + plan.unitCount; u++)
		{
			hcPutSingle(&filters[u], in);
			hcProcessSingle(&filters[u]);
			hcGetSingle(&filters[u], tempBuffer.data());
			const double factor = unitFactors[u];
			if (factor == 1.0)
			{
				for (unsigned f = 0; f < frameCount; f++)
					out[f] += tempBuffer[f];
			}
			else
			{
				for (unsigned f = 0; f < frameCount; f++)
					out[f] += factor * tempBuffer[f];
			}
		}
	}
}
#pragma AVRT_CODE_END

void MultiConvolutionFilter::cleanup()
{
	// Deferred report of the mute path that process() took on the audio thread.
	// It runs before the members are cleared so the initialized block size is
	// still available.
	if (frameCountMismatchLogged)
		LogF(kConvolverMuteReportFormat, kFrameCountMismatchLogPrefix,
			muteDiagnostics.firstMuteFrameCount.load(std::memory_order_relaxed), filterFrameCount,
			muteDiagnostics.muteCallCount.load(std::memory_order_relaxed));
	frameCountMismatchLogged = false;

	// HConvSingleArray::reset() runs the close-then-free sequence.
	filters = nullptr;
	// Release this filter's hold on the cached IR; the weak-ptr cache frees the
	// entry once the last user drops it.
	irEntry.reset();
	unitCount = 0;
	unitFactors.clear();
	plans.clear();
	tempBuffer.clear();
	filterFrameCount = 0;
}
