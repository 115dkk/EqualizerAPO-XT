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

#include "engine/IFilter.h"
#include "runtime/memory/AlignedMemory.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"

#pragma AVRT_VTABLES_BEGIN
class VSTPluginFilter : public IFilter
{
public:
	VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData, const std::unordered_map<std::wstring, float>& paramMap,
		bool stereoInput = false);
	VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData,
		const std::unordered_map<std::wstring, float>& paramMap, VST3BusContract busContract);
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

private:
	void cleanup();

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

	// Delay compensation buffers
	unsigned delayBufferLength = 0;
	std::vector<AlignedMemory::UniqueAllocation<double>> delayBuffers;
	AlignedMemory::UniqueAllocation<double> delayTempBuffer;
	unsigned delayBufferOffset = 0;

	bool skipProcessing = false;
	bool reportCrash = true;
	bool forceStereoInput = false;
	std::optional<VST3BusContract> busContract;
};
#pragma AVRT_VTABLES_END
