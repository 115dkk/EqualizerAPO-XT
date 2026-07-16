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
	if (channelCount > (std::numeric_limits<unsigned>::max)())
	{
		LogF(L"The VST plugin %s was assigned too many host channels; passing audio through.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
	}

	skipProcessing = false;

	MemoryHelper::UniqueObject<VSTPluginInstance> firstEffect;
	try
	{
		firstEffect = MemoryHelper::constructUnique<VSTPluginInstance>(library, 2);
	}
	catch (const std::bad_alloc&)
	{
		LogF(L"The VST plugin %s could not allocate its host instance; passing audio through.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
	}
	if (!firstEffect->initialize())
	{
		LogF(L"The VST plugin %s crashed during initialization.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
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
		return channelNames;
	}

	effectInputCount = static_cast<unsigned>(reportedInputCount);
	effectOutputCount = static_cast<unsigned>(reportedOutputCount);
	effectChannelCount = max(effectInputCount, effectOutputCount);
	if (effectChannelCount == 0)
	{
		skipProcessing = true;
		return channelNames;
	}

	// round up
	const size_t requiredEffectCount = channelCount / effectChannelCount + (channelCount % effectChannelCount != 0 ? 1 : 0);
	size_t paddedChannelCount = 0;
	if (!checkedMultiply(requiredEffectCount, effectChannelCount, paddedChannelCount))
	{
		LogF(L"The VST plugin %s reported metadata that overflows its padded channel count; passing audio through.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
	}
	effects.reserve(requiredEffectCount);
	effects.push_back(std::move(firstEffect));
	for (size_t i = 1; i < requiredEffectCount; i++)
	{
		try
		{
			effects.push_back(MemoryHelper::constructUnique<VSTPluginInstance>(library, 2));
		}
		catch (const std::bad_alloc&)
		{
			LogF(L"The VST plugin %s could not allocate instance %Iu; passing audio through.", libPath.c_str(), i);
			skipProcessing = true;
			return channelNames;
		}
		if (!effects[i]->initialize())
		{
			LogF(L"The VST plugin %s crashed during initialization.", libPath.c_str());
			skipProcessing = true;
			return channelNames;
		}

		const int instanceInputCount = effects[i]->numInputs();
		const int instanceOutputCount = effects[i]->numOutputs();
		const int instanceLatency = effects[i]->getInitialDelay();
		if (instanceInputCount != reportedInputCount
			|| instanceOutputCount != reportedOutputCount
			|| instanceLatency != reportedLatency)
		{
			LogF(L"The VST plugin %s reported inconsistent per-instance metadata; passing audio through.", libPath.c_str());
			skipProcessing = true;
			return channelNames;
		}
	}

	prepareForProcessing(sampleRate, maxFrameCount);
	if (skipProcessing)
		return channelNames;

	// 2 times for input and output
	if (paddedChannelCount < channelCount
		|| paddedChannelCount - channelCount > (std::numeric_limits<size_t>::max)() / 2)
	{
		LogF(L"The VST plugin %s reported metadata that overflows its padding count; passing audio through.", libPath.c_str());
		skipProcessing = true;
		return channelNames;
	}
	const size_t emptyChannelCount = 2 * (paddedChannelCount - channelCount);
	emptyChannels.reserve(emptyChannelCount);
	for (size_t i = 0; i < emptyChannelCount; i++)
	{
		auto channel = MemoryHelper::allocateArray<double>(maxFrameCount);
		if (!channel)
		{
			LogF(L"The VST plugin %s could not allocate padding channel %Iu; passing audio through.", libPath.c_str(), i);
			skipProcessing = true;
			return channelNames;
		}
		std::fill_n(channel.get(), maxFrameCount, 0.0);
		emptyChannels.push_back(std::move(channel));
	}

	inputArray.resize(effectInputCount);
	outputArray.resize(effectOutputCount);

	// Allocate float buffers for conversion
	if (effectInputCount > 0) {
		// A hostile or broken plugin can report a bus count whose product with
		// maxFrameCount wraps before widening to size_t (CodeQL
		// cpp/integer-multiplication-cast-to-long); validate in size_t first.
		const size_t inputCount = effectInputCount;
		const size_t maxSize = (std::numeric_limits<size_t>::max)();
		if (maxFrameCount != 0 && inputCount > maxSize / maxFrameCount)
			throw std::bad_alloc();

		floatInputs.resize(inputCount);
		floatInputBuffer = MemoryHelper::allocateArray<float>(inputCount * maxFrameCount);
		if (!floatInputBuffer)
		{
			LogF(L"The VST plugin %s could not allocate float input buffers; passing audio through.", libPath.c_str());
			skipProcessing = true;
			return channelNames;
		}
		for (unsigned i = 0; i < effectInputCount; ++i) {
			floatInputs[i] = floatInputBuffer.get() + i * maxFrameCount;
		}
	}

	if (effectOutputCount > 0) {
		// Same wrap-before-widening hazard as the input buffers above.
		const size_t outputCount = effectOutputCount;
		const size_t maxSize = (std::numeric_limits<size_t>::max)();
		if (maxFrameCount != 0 && outputCount > maxSize / maxFrameCount)
			throw std::bad_alloc();

		floatOutputs.resize(outputCount);
		floatOutputBuffer = MemoryHelper::allocateArray<float>(outputCount * maxFrameCount);
		if (!floatOutputBuffer)
		{
			LogF(L"The VST plugin %s could not allocate float output buffers; passing audio through.", libPath.c_str());
			skipProcessing = true;
			return channelNames;
		}
		for (unsigned i = 0; i < effectOutputCount; ++i) {
			floatOutputs[i] = floatOutputBuffer.get() + i * maxFrameCount;
		}
	}

	// Allocate delay compensation buffers
	delayBufferLength = static_cast<unsigned>(reportedLatency);
	if (delayBufferLength > 0)
	{
		delayBuffers.reserve(channelCount);
		for (size_t i = 0; i < channelCount; i++)
		{
			auto buffer = MemoryHelper::allocateArray<double>(delayBufferLength);
			if (!buffer)
			{
				LogF(L"The VST plugin %s could not allocate delay buffer %Iu; passing audio through.", libPath.c_str(), i);
				skipProcessing = true;
				delayBufferLength = 0;
				return channelNames;
			}
			std::fill_n(buffer.get(), delayBufferLength, 0.0);
			delayBuffers.push_back(std::move(buffer));
		}
		delayTempBuffer = MemoryHelper::allocateArray<double>(maxFrameCount);
		if (!delayTempBuffer)
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
		for (size_t i = 0; i < effects.size(); i++)
		{
			VSTPluginInstance* effect = effects[i].get();

			if (i == effects.size() - 1 && (channelCount % effectChannelCount) != 0)
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
		for (size_t i = 0; i < effects.size(); i++)
		{
			VSTPluginInstance* effect = effects[i].get();
			// Setup double pointer arrays to point to the correct source/destination double buffers
			for (unsigned j = 0; j < effectInputCount; j++)
			{
				if (channelOffset + j < channelCount)
					inputArray[j] = input[channelOffset + j];
				else
					inputArray[j] = emptyChannels[emptyChannelIndex++].get();
			}

			for (unsigned j = 0; j < effectOutputCount; j++)
			{
				if (channelOffset + j < channelCount)
					outputArray[j] = output[channelOffset + j];
				else
					outputArray[j] = emptyChannels[emptyChannelIndex++].get();
			}

			if (effect->canDoubleReplacing()) {
				effect->processDoubleReplacing(inputArray.data(), outputArray.data(), frameCount);
			}
			else {
				// Convert input from double** to float** using pre-allocated buffers
				for (unsigned j = 0; j < effectInputCount; j++)
				{
					convertDoubleToFloat(floatInputs[j], inputArray[j], frameCount);
				}

				if (effect->canReplacing())
				{
					effect->processReplacing(floatInputs.data(), floatOutputs.data(), frameCount);
				}
				else
				{
					// For non-replacing, VST expects to add to the output. Clear float buffer first.
					for (unsigned j = 0; j < effectOutputCount; j++)
						std::fill_n(floatOutputs[j], frameCount, 0.0f);
					effect->process(floatInputs.data(), floatOutputs.data(), frameCount);
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
		if (!delayBuffers.empty() && delayBufferLength > 0)
		{
			for (unsigned i = 0; i < channelCount; i++)
			{
				double* outputChannel = output[i];
				double* delayBuffer = delayBuffers[i].get();

				if (delayBufferLength <= frameCount)
				{
					std::copy_n(outputChannel + frameCount - delayBufferLength, delayBufferLength, delayTempBuffer.get());
					std::copy_backward(outputChannel, outputChannel + frameCount - delayBufferLength, outputChannel + frameCount);
					std::copy_n(delayBuffer + delayBufferOffset, delayBufferLength - delayBufferOffset, outputChannel);
					std::copy_n(delayBuffer, delayBufferOffset, outputChannel + delayBufferLength - delayBufferOffset);
					std::copy_n(delayTempBuffer.get(), delayBufferLength, delayBuffer);
				}
				else
				{
					std::copy_n(outputChannel, frameCount, delayTempBuffer.get());

					if (delayBufferLength < delayBufferOffset + frameCount)
					{
						// Wrapping around the delay buffer
						std::copy_n(delayBuffer + delayBufferOffset, delayBufferLength - delayBufferOffset, outputChannel);
						std::copy_n(delayBuffer, frameCount - (delayBufferLength - delayBufferOffset), outputChannel + delayBufferLength - delayBufferOffset);
						std::copy_n(delayTempBuffer.get(), delayBufferLength - delayBufferOffset, delayBuffer + delayBufferOffset);
						std::copy_n(delayTempBuffer.get() + delayBufferLength - delayBufferOffset, frameCount - (delayBufferLength - delayBufferOffset), delayBuffer);
					}
					else
					{
						// Simple case - no wrapping
						std::copy_n(delayBuffer + delayBufferOffset, frameCount, outputChannel);
						std::copy_n(delayTempBuffer.get(), frameCount, delayBuffer + delayBufferOffset);
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
	for (const auto& effect : effects)
		effect->stopProcessingSafely();
	effects.clear();
	effectInputCount = 0;
	effectOutputCount = 0;
	effectChannelCount = 0;

	emptyChannels.clear();
	inputArray.clear();
	outputArray.clear();
	floatInputs.clear();
	floatInputBuffer.reset();
	floatOutputs.clear();
	floatOutputBuffer.reset();
	delayBuffers.clear();
	delayTempBuffer.reset();
	delayBufferLength = 0;
	delayBufferOffset = 0;
}
