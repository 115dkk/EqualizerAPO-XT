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

#include "stdafx.h"
#include <limits>
#include <new>
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "VSTPluginFilter.h"

using std::max;

namespace
{
constexpr unsigned kMaxPluginChannelCount = 1024;
constexpr unsigned kMaxPluginLatencySamples = 16 * 1024 * 1024;

bool checkedMultiply(size_t left, size_t right, size_t& result) noexcept
{
	if (left != 0 && right > (std::numeric_limits<size_t>::max)() / left)
		return false;
	result = left * right;
	return true;
}
}

VSTPluginFilter::VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData, const std::unordered_map<std::wstring, float>& paramMap)
	: library(library), libPath(library->getLibPath()), chunkData(chunkData), paramMap(paramMap)
{
}

VSTPluginFilter::~VSTPluginFilter()
{
	cleanup();
}

std::vector<std::wstring> VSTPluginFilter::initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	cleanup();

	channelCount = channelNames.size();
	if (channelCount == 0)
		return channelNames;

	skipProcessing = false;

	// MemoryHelper::alloc returns nullptr on failure (and logs the size). Every
	// result below is checked: on failure the filter degrades to skipProcessing
	// (process() passes audio through) with the already-built state left
	// consistent for cleanup().
	void* mem = MemoryHelper::alloc(sizeof(VSTPluginInstance));
	if (mem == nullptr)
	{
		LogF(L"The VST plugin %s could not allocate its host instance; passing audio through.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
	}
	VSTPluginInstance* firstEffect = new(mem) VSTPluginInstance(library, 2);
	if (!firstEffect->initialize())
	{
		LogF(L"The VST plugin %s crashed during initialization.", libPath.c_str());
		skipProcessing = true;
	}

	// Metadata is plugin-controlled. Snapshot it once, validate the signed
	// values, and use only the cached values for every allocation and processing
	// loop below. Re-reading allows a broken plugin to change the loop bounds
	// after the corresponding buffers were sized.
	const int reportedInputCount = firstEffect->numInputs();
	const int reportedOutputCount = firstEffect->numOutputs();
	const int reportedLatency = firstEffect->getInitialDelay();
	if (reportedInputCount < 0 || reportedOutputCount < 0 || reportedLatency < 0
		|| reportedInputCount > static_cast<int>(kMaxPluginChannelCount)
		|| reportedOutputCount > static_cast<int>(kMaxPluginChannelCount)
		|| reportedLatency > static_cast<int>(kMaxPluginLatencySamples))
	{
		LogF(L"The VST plugin %s reported invalid channel or latency metadata; passing audio through.", libPath.c_str());
		skipProcessing = true;
		firstEffect->~VSTPluginInstance();
		MemoryHelper::free(firstEffect);
		return channelNames;
	}

	effectInputCount = static_cast<unsigned>(reportedInputCount);
	effectOutputCount = static_cast<unsigned>(reportedOutputCount);
	effectChannelCount = max(effectInputCount, effectOutputCount);
	if (effectChannelCount == 0)
	{
		skipProcessing = true;
		firstEffect->~VSTPluginInstance();
		MemoryHelper::free(firstEffect);
		return channelNames;
	}

	// round up
	effectCount = channelCount / effectChannelCount + (channelCount % effectChannelCount != 0 ? 1 : 0);
	size_t allocationBytes = 0;
	if (!checkedMultiply(effectCount, sizeof(VSTPluginInstance*), allocationBytes))
	{
		LogF(L"The VST plugin %s reported metadata that overflows its instance table; passing audio through.", libPath.c_str());
		skipProcessing = true;
		effectCount = 0;
		firstEffect->~VSTPluginInstance();
		MemoryHelper::free(firstEffect);
		return channelNames;
	}
	effects = (VSTPluginInstance**)MemoryHelper::alloc(allocationBytes);
	if (effects == nullptr)
	{
		LogF(L"The VST plugin %s could not allocate its instance table; passing audio through.", libPath.c_str());
		skipProcessing = true;
		effectCount = 0;
		firstEffect->~VSTPluginInstance();
		MemoryHelper::free(firstEffect);
		return channelNames;
	}
	effects[0] = firstEffect;
	for (unsigned i = 1; i < effectCount; i++)
	{
		mem = MemoryHelper::alloc(sizeof(VSTPluginInstance));
		if (mem == nullptr)
		{
			LogF(L"The VST plugin %s could not allocate instance %u; passing audio through.", libPath.c_str(), i);
			skipProcessing = true;
			// cleanup() tears down exactly [0, effectCount); entries past i were
			// never constructed.
			effectCount = i;
			return channelNames;
		}
		effects[i] = new(mem) VSTPluginInstance(library, 2);
		if (!effects[i]->initialize() && !skipProcessing)
		{
			LogF(L"The VST plugin %s crashed during initialization.", libPath.c_str());
			skipProcessing = true;
		}
	}

	prepareForProcessing(sampleRate, maxFrameCount);

	// 2 times for input and output
	size_t paddedChannelCount = 0;
	if (!checkedMultiply(effectCount, effectChannelCount, paddedChannelCount)
		|| paddedChannelCount < channelCount
		|| paddedChannelCount - channelCount > (std::numeric_limits<size_t>::max)() / 2)
	{
		LogF(L"The VST plugin %s reported metadata that overflows its padding count; passing audio through.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
	}
	emptyChannelCount = 2 * (paddedChannelCount - channelCount);
	if (!checkedMultiply(emptyChannelCount, sizeof(double*), allocationBytes))
	{
		LogF(L"The VST plugin %s reported metadata that overflows its padding table; passing audio through.", libPath.c_str());
		skipProcessing = true;
		emptyChannelCount = 0;
		return channelNames;
	}
	emptyChannels = emptyChannelCount > 0
		? (double**)MemoryHelper::alloc(allocationBytes)
		: nullptr;
	if (emptyChannels == nullptr && emptyChannelCount > 0)
	{
		LogF(L"The VST plugin %s could not allocate padding channels; passing audio through.", libPath.c_str());
		skipProcessing = true;
		emptyChannelCount = 0;
		return channelNames;
	}
	for (unsigned i = 0; i < emptyChannelCount; i++)
	{
		if (!checkedMultiply(maxFrameCount, sizeof *emptyChannels[i], allocationBytes))
		{
			skipProcessing = true;
			emptyChannelCount = i;
			return channelNames;
		}
		emptyChannels[i] = static_cast<double*>(MemoryHelper::alloc(allocationBytes));
		if (emptyChannels[i] == nullptr)
		{
			LogF(L"The VST plugin %s could not allocate padding channel %u; passing audio through.", libPath.c_str(), i);
			skipProcessing = true;
			// Entries past i are uninitialized; shrink the count so cleanup()
			// frees only what was built.
			emptyChannelCount = i;
			return channelNames;
		}
		std::fill_n(emptyChannels[i], maxFrameCount, 0.0);
	}

	if (!checkedMultiply(effectInputCount, sizeof(double*), allocationBytes))
	{
		LogF(L"The VST plugin %s reported an input count that overflows its bus table; passing audio through.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
	}
	inputArray = effectInputCount > 0 ? (double**)MemoryHelper::alloc(allocationBytes) : nullptr;
	if (!checkedMultiply(effectOutputCount, sizeof(double*), allocationBytes))
	{
		LogF(L"The VST plugin %s reported an output count that overflows its bus table; passing audio through.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
	}
	outputArray = effectOutputCount > 0 ? (double**)MemoryHelper::alloc(allocationBytes) : nullptr;
	if ((effectInputCount > 0 && inputArray == nullptr)
		|| (effectOutputCount > 0 && outputArray == nullptr))
	{
		LogF(L"The VST plugin %s could not allocate its bus arrays; passing audio through.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
	}

	// Allocate float buffers for conversion
	if (effectInputCount > 0) {
		// A hostile or broken plugin can report a bus count whose product with
		// maxFrameCount wraps before widening to size_t (CodeQL
		// cpp/integer-multiplication-cast-to-long); validate in size_t first.
		const size_t inputCount = effectInputCount;
		const size_t maxSize = (std::numeric_limits<size_t>::max)();
		if (inputCount > maxSize / sizeof(float*) ||
			(maxFrameCount != 0 &&
				inputCount > maxSize / maxFrameCount / sizeof *_floatInputBuffer))
			throw std::bad_alloc();

		floatInputs = (float**)MemoryHelper::alloc(inputCount * sizeof(float*));
		_floatInputBuffer = static_cast<float*>(MemoryHelper::alloc(inputCount * maxFrameCount * sizeof *_floatInputBuffer));
		if (floatInputs == nullptr || _floatInputBuffer == nullptr)
		{
			LogF(L"The VST plugin %s could not allocate float input buffers; passing audio through.", libPath.c_str());
			skipProcessing = true;
			return channelNames;
		}
		for (unsigned i = 0; i < effectInputCount; ++i) {
			floatInputs[i] = _floatInputBuffer + i * maxFrameCount;
		}
	}

	if (effectOutputCount > 0) {
		// Same wrap-before-widening hazard as the input buffers above.
		const size_t outputCount = effectOutputCount;
		const size_t maxSize = (std::numeric_limits<size_t>::max)();
		if (outputCount > maxSize / sizeof(float*) ||
			(maxFrameCount != 0 &&
				outputCount > maxSize / maxFrameCount / sizeof *_floatOutputBuffer))
			throw std::bad_alloc();

		floatOutputs = (float**)MemoryHelper::alloc(outputCount * sizeof(float*));
		_floatOutputBuffer = static_cast<float*>(MemoryHelper::alloc(outputCount * maxFrameCount * sizeof *_floatOutputBuffer));
		if (floatOutputs == nullptr || _floatOutputBuffer == nullptr)
		{
			LogF(L"The VST plugin %s could not allocate float output buffers; passing audio through.", libPath.c_str());
			skipProcessing = true;
			return channelNames;
		}
		for (unsigned i = 0; i < effectOutputCount; ++i) {
			floatOutputs[i] = _floatOutputBuffer + i * maxFrameCount;
		}
	}

	// Allocate delay compensation buffers
	delayBufferLength = static_cast<unsigned>(reportedLatency);
	if (delayBufferLength > 0)
	{
		if (!checkedMultiply(channelCount, sizeof(double*), allocationBytes))
		{
			LogF(L"The VST plugin %s reported a latency whose channel table overflows; passing audio through.", libPath.c_str());
			skipProcessing = true;
			delayBufferLength = 0;
			return channelNames;
		}
		delayBuffers = (double**)MemoryHelper::alloc(allocationBytes);
		if (delayBuffers == nullptr)
		{
			LogF(L"The VST plugin %s could not allocate delay buffers; passing audio through.", libPath.c_str());
			skipProcessing = true;
			delayBufferLength = 0;
			return channelNames;
		}
		for (unsigned i = 0; i < channelCount; i++)
		{
			if (!checkedMultiply(delayBufferLength, sizeof *delayBuffers[i], allocationBytes))
			{
				for (unsigned j = 0; j < i; j++)
					MemoryHelper::free(delayBuffers[j]);
				MemoryHelper::free(delayBuffers);
				delayBuffers = nullptr;
				delayBufferLength = 0;
				skipProcessing = true;
				return channelNames;
			}
			delayBuffers[i] = static_cast<double*>(MemoryHelper::alloc(allocationBytes));
			if (delayBuffers[i] == nullptr)
			{
				LogF(L"The VST plugin %s could not allocate delay buffer %u; passing audio through.", libPath.c_str(), i);
				skipProcessing = true;
				// cleanup() walks all channelCount entries; free what was built
				// and drop the array so it never reads the uninitialized tail.
				for (unsigned j = 0; j < i; j++)
					MemoryHelper::free(delayBuffers[j]);
				MemoryHelper::free(delayBuffers);
				delayBuffers = nullptr;
				delayBufferLength = 0;
				return channelNames;
			}
			std::fill_n(delayBuffers[i], delayBufferLength, 0.0);
		}
		if (!checkedMultiply(maxFrameCount, sizeof *delayTempBuffer, allocationBytes))
		{
			skipProcessing = true;
			delayBufferLength = 0;
			return channelNames;
		}
		delayTempBuffer = static_cast<double*>(MemoryHelper::alloc(allocationBytes));
		if (delayTempBuffer == nullptr)
		{
			LogF(L"The VST plugin %s could not allocate its delay scratch buffer; passing audio through.", libPath.c_str());
			skipProcessing = true;
			delayBufferLength = 0;
			return channelNames;
		}
		delayBufferOffset = 0;
	}

	return channelNames;
}

void VSTPluginFilter::prepareForProcessing(float sampleRate, unsigned maxFrameCount)
{
	__try
	{
		for (unsigned i = 0; i < effectCount; i++)
		{
			VSTPluginInstance* effect = effects[i];

			if (i == effectCount - 1 && (channelCount % effectChannelCount) != 0)
				effect->setUsedChannelCount(channelCount % effectChannelCount);
			else
				effect->setUsedChannelCount(effectChannelCount);
			effect->prepareForProcessing(sampleRate, maxFrameCount);
			effect->writeToEffect(chunkData, paramMap);
			effect->startProcessing();
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		LogF(L"The VST plugin %s crashed while preparing for processing.", libPath.c_str());
		skipProcessing = true;
	}
}

#pragma AVRT_CODE_BEGIN
void convertFloatToDouble(double* dest, const float* src, size_t count);

// Converts a block of doubles back to floats.
void convertDoubleToFloat(float* dest, const double* src, size_t count);

void VSTPluginFilter::process(double** output, double** input, unsigned frameCount)
{
	if (skipProcessing)
	{
		for (unsigned i = 0; i < channelCount; i++)
			std::copy_n(input[i], frameCount, output[i]);
		return;
	}

	__try
	{
		unsigned channelOffset = 0;
		unsigned emptyChannelIndex = 0;
		for (unsigned i = 0; i < effectCount; i++)
		{
			VSTPluginInstance* effect = effects[i];
			// Setup double pointer arrays to point to the correct source/destination double buffers
			for (unsigned j = 0; j < effectInputCount; j++)
			{
				if (channelOffset + j < channelCount)
					inputArray[j] = input[channelOffset + j];
				else
					inputArray[j] = emptyChannels[emptyChannelIndex++];
			}

			for (unsigned j = 0; j < effectOutputCount; j++)
			{
				if (channelOffset + j < channelCount)
					outputArray[j] = output[channelOffset + j];
				else
					outputArray[j] = emptyChannels[emptyChannelIndex++];
			}

			if (effect->canDoubleReplacing()) {
				effect->processDoubleReplacing(inputArray, outputArray, frameCount);
			}
			else {
				// Convert input from double** to float** using pre-allocated buffers
				for (unsigned j = 0; j < effectInputCount; j++)
				{
					convertDoubleToFloat(floatInputs[j], inputArray[j], frameCount);
				}

				if (effect->canReplacing())
				{
					effect->processReplacing(floatInputs, floatOutputs, frameCount);
				}
				else
				{
					// For non-replacing, VST expects to add to the output. Clear float buffer first.
					for (unsigned j = 0; j < effectOutputCount; j++)
						std::fill_n(floatOutputs[j], frameCount, 0.0f);
					effect->process(floatInputs, floatOutputs, frameCount);
				}

				// Convert output from float** back to double** into the final destination
				for (unsigned j = 0; j < effectOutputCount; j++)
				{
					convertFloatToDouble(outputArray[j], floatOutputs[j], frameCount);
				}
			}

			if (effectOutputCount < effectInputCount)
			{
				for (unsigned j = effectOutputCount; j < effectInputCount; j++)
				{
					if (channelOffset + j < channelCount)
						std::fill_n(output[channelOffset + j], frameCount, 0.0);
				}
			}

			channelOffset += effectChannelCount;
		}

		// Apply delay compensation if needed
		if (delayBuffers != nullptr && delayBufferLength > 0)
		{
			for (unsigned i = 0; i < channelCount; i++)
			{
				double* outputChannel = output[i];
				double* delayBuffer = delayBuffers[i];

				if (delayBufferLength <= frameCount)
				{
					std::copy_n(outputChannel + frameCount - delayBufferLength, delayBufferLength, delayTempBuffer);
					std::copy_backward(outputChannel, outputChannel + frameCount - delayBufferLength, outputChannel + frameCount);
					std::copy_n(delayBuffer + delayBufferOffset, delayBufferLength - delayBufferOffset, outputChannel);
					std::copy_n(delayBuffer, delayBufferOffset, outputChannel + delayBufferLength - delayBufferOffset);
					std::copy_n(delayTempBuffer, delayBufferLength, delayBuffer);
				}
				else
				{
					std::copy_n(outputChannel, frameCount, delayTempBuffer);

					if (delayBufferLength < delayBufferOffset + frameCount)
					{
						// Wrapping around the delay buffer
						std::copy_n(delayBuffer + delayBufferOffset, delayBufferLength - delayBufferOffset, outputChannel);
						std::copy_n(delayBuffer, frameCount - (delayBufferLength - delayBufferOffset), outputChannel + delayBufferLength - delayBufferOffset);
						std::copy_n(delayTempBuffer, delayBufferLength - delayBufferOffset, delayBuffer + delayBufferOffset);
						std::copy_n(delayTempBuffer + delayBufferLength - delayBufferOffset, frameCount - (delayBufferLength - delayBufferOffset), delayBuffer);
					}
					else
					{
						// Simple case - no wrapping
						std::copy_n(delayBuffer + delayBufferOffset, frameCount, outputChannel);
						std::copy_n(delayTempBuffer, frameCount, delayBuffer + delayBufferOffset);
					}
				}
			}

			// Update buffer offset
			if (delayBufferLength <= frameCount)
				delayBufferOffset = 0;
			else
				delayBufferOffset = (delayBufferOffset + frameCount) % delayBufferLength;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		if (reportCrash)
		{
			LogF(L"The VST plugin %s crashed during audio processing.", libPath.c_str());
			reportCrash = false;
		}

		for (unsigned i = 0; i < channelCount; i++)
			std::copy_n(input[i], frameCount, output[i]);
	}
}
#pragma AVRT_CODE_END

std::shared_ptr<VSTPluginLibrary> VSTPluginFilter::getLibrary() const
{
	return library;
}

const std::wstring& VSTPluginFilter::getChunkData() const
{
	return chunkData;
}

const std::unordered_map<std::wstring, float>& VSTPluginFilter::getParamMap() const
{
	return paramMap;
}

void VSTPluginFilter::cleanup()
{
	if (effects != nullptr)
	{
		for (unsigned i = 0; i < effectCount; i++)
		{
			VSTPluginInstance* effect = effects[i];
			effect->stopProcessing();
			effect->~VSTPluginInstance();
			MemoryHelper::free(effect);
		}
		MemoryHelper::free(effects);
		effects = nullptr;
	}
	effectCount = 0;
	effectInputCount = 0;
	effectOutputCount = 0;
	effectChannelCount = 0;

	if (emptyChannels != nullptr)
	{
		for (unsigned i = 0; i < emptyChannelCount; i++)
			MemoryHelper::free(emptyChannels[i]);
		MemoryHelper::free(emptyChannels);
		emptyChannels = nullptr;
	}
	emptyChannelCount = 0;

	if (inputArray != nullptr)
	{
		MemoryHelper::free(inputArray);
		inputArray = nullptr;
	}

	if (outputArray != nullptr)
	{
		MemoryHelper::free(outputArray);
		outputArray = nullptr;
	}
    
    if (floatInputs != nullptr) {
		MemoryHelper::free(floatInputs);
		floatInputs = nullptr;
	}
	if (_floatInputBuffer != nullptr) {
		MemoryHelper::free(_floatInputBuffer);
		_floatInputBuffer = nullptr;
	}
	if (floatOutputs != nullptr) {
		MemoryHelper::free(floatOutputs);
		floatOutputs = nullptr;
	}
	if (_floatOutputBuffer != nullptr) {
		MemoryHelper::free(_floatOutputBuffer);
		_floatOutputBuffer = nullptr;
	}

	if (delayBuffers != nullptr)
	{
		for (unsigned i = 0; i < channelCount; i++)
			MemoryHelper::free(delayBuffers[i]);
		MemoryHelper::free(delayBuffers);
		delayBuffers = nullptr;
	}
	if (delayTempBuffer != nullptr)
	{
		MemoryHelper::free(delayTempBuffer);
		delayTempBuffer = nullptr;
	}
	delayBufferLength = 0;
	delayBufferOffset = 0;
}
