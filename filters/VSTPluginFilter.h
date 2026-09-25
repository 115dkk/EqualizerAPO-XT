/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

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

#include <vector>
#include <optional>

#include "dsp/DelayLine.h"
#include "engine/IFilter.h"
#include "runtime/memory/AlignedMemory.h"
#include "filters/VSTChannelPlan.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"

#pragma AVRT_VTABLES_BEGIN
class VSTPluginFilter : public IFilter
{
public:
	VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData, const std::unordered_map<std::wstring, float>& paramMap,
		bool stereoInput = false);
	VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData,
		const std::unordered_map<std::wstring, float>& paramMap, VST3BusContract busContract,
		std::vector<std::wstring> inputChannels = std::vector<std::wstring>(),
		std::vector<std::wstring> outputChannels = std::vector<std::wstring>());
	~VSTPluginFilter();

	bool getInPlace() override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void prepareForProcessing(float sampleRate, unsigned maxFrameCount);
	void process(double** output, double** input, unsigned frameCount) override;

	std::shared_ptr<VSTPluginLibrary> getLibrary() const;
	const std::wstring& getChunkData() const;
	const std::unordered_map<std::wstring, float>& getParamMap() const;
	bool getStereoInput() const;
	const std::optional<VST3BusContract>& getBusContract() const;
	const std::vector<std::wstring>& getInputChannels() const;
	const std::vector<std::wstring>& getOutputChannels() const;

private:
	// What initialize() carries from one step to the next.
	struct InitContext
	{
		AlignedMemory::UniqueObject<VSTPluginInstance> firstEffect;
		bool upmixerLayout = false;
		int reportedInputCount = 0;
		int reportedOutputCount = 0;
		int reportedLatency = 0;
		VSTChannelPlan plan;
	};

	void cleanup();
	// Logs format (libPath first, then args), sets skipProcessing and returns
	// false, so a step can end initialize() with `return passThrough(...)`.
	template<class... Args>
	bool passThrough(const wchar_t* format, Args... args);
	// Brings one instance to the bus layout this line asks for.
	bool negotiateInstance(VSTPluginInstance* effect, unsigned targetChannelCount,
		const std::vector<std::wstring>& outputChannelNames, bool upmixerLayout);
	// Creates, initializes and negotiates the full-width first instance.
	bool createFirstInstance(InitContext& context, const std::vector<std::wstring>& channelNames);
	// Snapshots and validates the channel and latency metadata it reported.
	bool readMetadata(InitContext& context);
	// Maps the channels onto instances (planVstChannels) and keeps the fill.
	bool planChannels(InitContext& context, const std::vector<std::wstring>& channelNames);
	// Re-negotiates a split first instance and creates the other instances.
	bool createRemainingInstances(InitContext& context, const std::vector<std::wstring>& channelNames);
	// Padding, "-" scratch and float conversion buffers.
	bool allocateBuffers(const InitContext& context, unsigned maxFrameCount);
	// The latency compensation ring for the channels the plugin does not write.
	bool allocateDelayCompensation(const InitContext& context, unsigned maxFrameCount);

	std::shared_ptr<VSTPluginLibrary> library;
	std::wstring libPath;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	size_t channelCount = 0;
	unsigned effectInputCount = 0;
	unsigned effectOutputCount = 0;
	unsigned effectChannelCount = 0;
	std::vector<AlignedMemory::UniqueObject<VSTPluginInstance>> effects;
	std::vector<AlignedMemory::UniqueAllocation<double>> emptyChannels;
	std::vector<double*> inputArray;
	std::vector<double*> outputArray;

	// Buffers for float conversion
	std::vector<float*> floatInputs;
	AlignedMemory::UniqueAllocation<float> floatInputBuffer;
	std::vector<float*> floatOutputs;
	AlignedMemory::UniqueAllocation<float> floatOutputBuffer;

	// Delay compensation for the latency the plugin reported, on the channels
	// no plugin output writes; delayedOutputs is sized in initialize().
	DelayLine latencyDelay;
	std::vector<unsigned> delayedChannels;
	std::vector<double*> delayedOutputs;

	bool skipProcessing = false;
	bool reportCrash = true;
	bool forceStereoInput = false;
	std::optional<VST3BusContract> busContract;
	std::vector<std::wstring> inputChannels;
	std::vector<std::wstring> outputChannels;
	std::vector<int> resolvedInputChannels;
	std::vector<int> resolvedOutputChannels;
	std::vector<unsigned> passthroughChannels;
};
#pragma AVRT_VTABLES_END
