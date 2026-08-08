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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "IFilter.h"

// The stream facts a FilterConfiguration is built for (audit #250 A2). The
// constructor used to take a FilterEngine* and read exactly these three
// values once; taking the values themselves lets tests build a configuration
// without an engine, a config file or a registry read.
struct EngineStreamFormat
{
	unsigned realChannelCount = 0;
	unsigned outputChannelCount = 0;
	unsigned maxFrameCount = 0;
};

struct FilterInfo
{
	std::unique_ptr<IFilter, FilterDeleter> filter;
	bool inPlace = true;
	std::vector<size_t> inChannels;
	std::vector<size_t> outChannels;
	// The filter's type name, for the profiler's scope label. Resolved when the
	// configuration is built rather than in process(): typeid(x).name() allocates
	// inside the CRT the first time it is asked about a type, and process() runs
	// on the audio thread. It was the last first-call allocation left there, and
	// only when profiling was switched on - which is exactly when a first call
	// happens for every filter type at once.
	//
	// Points into the type's static type_info, so it outlives everything here and
	// needs no storage of its own.
	const char* profileLabel = "filter";
};

#pragma AVRT_VTABLES_BEGIN
class FilterConfiguration
{
public:
	FilterConfiguration(const EngineStreamFormat& format, std::vector<std::unique_ptr<FilterInfo>> filterInfos, unsigned allChannelCount);
	~FilterConfiguration();

	void read(double* input, unsigned frameCount);
	void read(double** input, unsigned frameCount);
	// Fused float32 -> double + deinterleave (or planar copy) directly into the
	// internal planar storage. Used by the APO when the connection is float32,
	// avoiding an intermediate conversion buffer.
	void readFloatInterleaved(const float* input, unsigned frameCount);
	void readFloatPlanar(const float* const* input, unsigned frameCount);
	void process(unsigned frameCount);
	// factorTable[i] holds the equal-power crossfade factor for the i-th frame
	// of the transition, precomputed by the engine; i >= transitionLength implies
	// the fade is complete (factor = 1.0). Passing the table avoids a per-frame
	// std::cos call inside the audio hot path.
	unsigned doTransition(FilterConfiguration* nextConfig, unsigned frameCount, unsigned transitionCounter, unsigned transitionLength, const double* factorTable);
	void write(double* output, unsigned frameCount);
	void write(double** output, unsigned frameCount);
	// Fused double -> float32 + interleave (or planar copy) directly from the
	// internal planar storage. Mirrors the readFloat variants.
	void writeFloatInterleaved(float* output, unsigned frameCount);
	void writeFloatPlanar(float* const* output, unsigned frameCount);
	double** getOutputSamples() {return allSamples.data();}
	bool isEmpty();
	// True if every filter in this configuration guarantees silent output for
	// silent input (no cross-block state, no tail). Cached at construction.
	bool isAllStateless() const { return allStateless; }

private:
	unsigned realChannelCount;
	unsigned outputChannelCount;
	unsigned allChannelCount;
	std::vector<double> allSamplesData;
	std::vector<double> allSamples2Data;
	std::vector<double*> allSamples;
	std::vector<double*> allSamples2;
	std::vector<double*> currentSamples;
	std::vector<double*> currentSamples2;
	std::vector<std::unique_ptr<FilterInfo>> filterInfos;
	bool allStateless = false;
};
#pragma AVRT_VTABLES_END
