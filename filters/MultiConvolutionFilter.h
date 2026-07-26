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

#include <memory>
#include <string>
#include <vector>

#include "engine/IFilter.h"
#include "IrCache.h"
#include "MultiConvolutionCommand.h"

// Multi-input synthesis convolution ("다중 합성 컨볼루션"). Unlike the 1:1
// ConvolutionFilter, this filter convolves each mapping target's own
// (pre-command) signal with the listed channels of a single multi-channel
// impulse response and sums the results into that target. It compresses the
// Copy -> Channel -> Convolution -> Copy fan-out/sum pattern that true-stereo
// and BRIR processing needs into one line, and it is independent of the
// Channel command: getAllChannels() asks the engine for every channel, and the
// participating impulse-response channels come from the command line and the
// file itself.
//
// getInPlace() is false so the engine hands us separate output buffers; every
// mapping therefore reads the pre-command state of its target while writing
// the new one, exactly like Copy.
#pragma AVRT_VTABLES_BEGIN
class MultiConvolutionFilter : public IFilter
{
public:
	MultiConvolutionFilter(const std::vector<MultiConvolutionCommand::Mapping>& mappings, const std::wstring& filename);
	virtual ~MultiConvolutionFilter();
	bool getAllChannels() override {return true;}
	bool getInPlace() override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	void cleanup();

	std::vector<MultiConvolutionCommand::Mapping> mappings;
	std::wstring filename;
	float sampleRate;
	unsigned filterFrameCount;
	bool frameCountMismatchLogged;

	// Where one mapping reads and writes: units [firstUnit, firstUnit+unitCount)
	// of the flat convolution-state array feed output[outputSlot] from
	// input[inputChannel]. inputChannel is -1 when the target does not exist yet
	// (a fresh virtual channel), which reads as silence.
	struct MappingPlan
	{
		unsigned outputSlot;
		int inputChannel;
		unsigned firstUnit;
		unsigned unitCount;
	};
	std::vector<MappingPlan> plans;

	// One convolution state per (mapping, impulse-response channel) pair, laid
	// out mapping by mapping; plans[] holds the per-mapping ranges. The holder
	// runs the close-then-free teardown.
	HConvSingleArray filters;
	unsigned unitCount;
	// Linear scale per unit (dB factors already converted), aligned with the
	// flat convolution-state array.
	std::vector<double> unitFactors;
	// Pins the cached impulse response for this filter's lifetime; the
	// process-wide cache holds only weak references.
	std::shared_ptr<const IrCacheEntry> irEntry;
	// Scratch buffer for one unit's convolution result before it is summed into
	// the mapping's output. Sized in initialize() so process() never allocates
	// on the audio thread.
	std::vector<double> tempBuffer;
};
#pragma AVRT_VTABLES_END
