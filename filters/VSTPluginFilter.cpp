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
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "VSTPluginFilter.h"

using std::max;

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

	void* mem = MemoryHelper::alloc(sizeof(VSTPluginInstance));
	VSTPluginInstance* firstEffect = new(mem) VSTPluginInstance(library, 2);
	if (!firstEffect->initialize())
	{
		LogF(L"The VST plugin %s crashed during initialization.", libPath.c_str());
		skipProcessing = true;
	}

	effectChannelCount = max(firstEffect->numInputs(), firstEffect->numOutputs());
	if (effectChannelCount == 0)
	{
		skipProcessing = true;
		firstEffect->~VSTPluginInstance();
		MemoryHelper::free(firstEffect);
		return channelNames;
	}

	// round up
	effectCount = (channelCount + (effectChannelCount - 1)) / effectChannelCount;
	effects = (VSTPluginInstance**)MemoryHelper::alloc(effectCount * sizeof(VSTPluginInstance*));
	effects[0] = firstEffect;
	for (unsigned i = 1; i < effectCount; i++)
	{
		mem = MemoryHelper::alloc(sizeof(VSTPluginInstance));
		effects[i] = new(mem) VSTPluginInstance(library, 2);
		if (!effects[i]->initialize() && !skipProcessing)
		{
			LogF(L"The VST plugin %s crashed during initialization.", libPath.c_str());
			skipProcessing = true;
		}
	}

	prepareForProcessing(sampleRate, maxFrameCount);

	// 2 times for input and output
	emptyChannelCount = 2 * (effectCount * effectChannelCount - channelCount);
	emptyChannels = (double**)MemoryHelper::alloc(emptyChannelCount * sizeof(double*));
	for (unsigned i = 0; i < emptyChannelCount; i++)
	{
		emptyChannels[i] = static_cast<double*>(MemoryHelper::alloc(maxFrameCount * sizeof *emptyChannels[i]));
		std::fill_n(emptyChannels[i], maxFrameCount, 0.0);
	}

	inputArray = (double**)MemoryHelper::alloc(firstEffect->numInputs() * sizeof(double*));
	outputArray = (double**)MemoryHelper::alloc(firstEffect->numOutputs() * sizeof(double*));

	// Allocate float buffers for conversion
	if (firstEffect->numInputs() > 0) {
		floatInputs = (float**)MemoryHelper::alloc(firstEffect->numInputs() * sizeof(float*));
		_floatInputBuffer = static_cast<float*>(MemoryHelper::alloc(firstEffect->numInputs() * maxFrameCount * sizeof *_floatInputBuffer));
		for (int i = 0; i < firstEffect->numInputs(); ++i) {
			floatInputs[i] = _floatInputBuffer + i * maxFrameCount;
		}
	}

	if (firstEffect->numOutputs() > 0) {
		floatOutputs = (float**)MemoryHelper::alloc(firstEffect->numOutputs() * sizeof(float*));
		_floatOutputBuffer = static_cast<float*>(MemoryHelper::alloc(firstEffect->numOutputs() * maxFrameCount * sizeof *_floatOutputBuffer));
		for (int i = 0; i < firstEffect->numOutputs(); ++i) {
			floatOutputs[i] = _floatOutputBuffer + i * maxFrameCount;
		}
	}

	// Allocate delay compensation buffers
	delayBufferLength = firstEffect->getInitialDelay();
	if (delayBufferLength > 0)
	{
		delayBuffers = (double**)MemoryHelper::alloc(channelCount * sizeof(double*));
		for (unsigned i = 0; i < channelCount; i++)
		{
			delayBuffers[i] = static_cast<double*>(MemoryHelper::alloc(delayBufferLength * sizeof *delayBuffers[i]));
			std::fill_n(delayBuffers[i], delayBufferLength, 0.0);
		}
		delayTempBuffer = static_cast<double*>(MemoryHelper::alloc(maxFrameCount * sizeof *delayTempBuffer));
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
			for (int j = 0; j < effect->numInputs(); j++)
			{
				if (channelOffset + j < channelCount)
					inputArray[j] = input[channelOffset + j];
				else
					inputArray[j] = emptyChannels[emptyChannelIndex++];
			}

			for (int j = 0; j < effect->numOutputs(); j++)
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
				for (int j = 0; j < effect->numInputs(); j++)
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
					for (int j = 0; j < effect->numOutputs(); j++)
						std::fill_n(floatOutputs[j], frameCount, 0.0f);
					effect->process(floatInputs, floatOutputs, frameCount);
				}

				// Convert output from float** back to double** into the final destination
				for (int j = 0; j < effect->numOutputs(); j++)
				{
					convertFloatToDouble(outputArray[j], floatOutputs[j], frameCount);
				}
			}

			if (effect->numOutputs() < effect->numInputs())
			{
				for (int j = effect->numOutputs(); j < effect->numInputs(); j++)
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
