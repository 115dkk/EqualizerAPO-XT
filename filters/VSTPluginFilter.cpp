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
#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include "services/logging/Logging.h"
#include "VSTPluginFilter.h"

using std::max;

namespace
{
constexpr unsigned kMaxPluginChannelCount = 1024;
constexpr unsigned kMaxPluginLatencySamples = 16 * 1024 * 1024;

// A plugin that declares itself an up/downmixer or spatializer processes a
// stereo source into the speaker layout; its input bus must stay stereo
// while only the output bus spans the device. The OpenSpatial Upmixer
// accepts a symmetric multichannel layout but leaves its engine disengaged
// there, which is why the declared role - not the accepted layout - drives
// the choice (probe evidence in PR #213).
bool isUpmixerSubCategory(const std::string& subCategories)
{
	std::string lower = subCategories;
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lower.find("up-downmix") != std::string::npos
		|| lower.find("updownmix") != std::string::npos
		|| lower.find("spatial") != std::string::npos
		|| lower.find("surround") != std::string::npos;
}

bool checkedMultiply(size_t left, size_t right, size_t& result) noexcept
{
	if (left != 0 && right > (std::numeric_limits<size_t>::max)() / left)
		return false;
	result = left * right;
	return true;
}
}

VSTPluginFilter::VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData, const std::unordered_map<std::wstring, float>& paramMap,
	bool stereoInput)
	: library(library), libPath(library->getLibPath()), chunkData(chunkData), paramMap(paramMap), forceStereoInput(stereoInput)
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

	AlignedMemory::UniqueObject<VSTPluginInstance> firstEffect;
	try
	{
		firstEffect = AlignedMemory::constructUnique<VSTPluginInstance>(library, 2);
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

	// A multichannel-capable plugin must see the full device width before its
	// channel counts are frozen below. Without this, the stereo probe from
	// initialize() becomes the permanent instance width, and a plugin that
	// analyzes the whole speaker layout at once (an upmixer expecting one 5.1
	// or 7.1 bus) is split into several stereo instances that each see only
	// two channels.
	//
	// Upmixer-type plugins additionally get a stereo input bus with the
	// full-width output bus: their engine keys on that asymmetric layout.
	// The role comes from the "StereoInput 1" config option or the VST3
	// subcategory; it is never inferred from accepted layouts alone, because
	// for anything but an upmixer a narrowed input bus would discard device
	// channels.
	const bool upmixerLayout = channelCount > 2
		&& (forceStereoInput || isUpmixerSubCategory(library->getVST3SubCategories()));
	const auto negotiateInstance = [upmixerLayout](VSTPluginInstance* effect, unsigned targetChannelCount,
		const std::vector<std::wstring>& outputChannelNames)
	{
		effect->setChannelNameHints(outputChannelNames);
		effect->negotiateChannelCount(static_cast<int>(targetChannelCount));
		if (upmixerLayout && targetChannelCount > 2)
		{
			const std::vector<std::wstring> stereoInputNames = {L"L", L"R"};
			effect->setBusChannelNameHints(stereoInputNames, outputChannelNames);
			effect->negotiateBusChannelCounts(2, static_cast<int>(targetChannelCount));
		}
	};
	if (channelCount <= kMaxPluginChannelCount)
		negotiateInstance(firstEffect.get(), static_cast<unsigned>(channelCount), channelNames);

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

	const auto channelNameSlice = [&channelNames](size_t offset, size_t width)
	{
		const size_t end = (std::min)(channelNames.size(), offset + width);
		if (offset >= end)
			return std::vector<std::wstring>();
		return std::vector<std::wstring>(channelNames.begin() + offset, channelNames.begin() + end);
	};

	// If the full-width proposal fell back to a narrower plugin layout, give
	// the first split instance the same per-instance name slice as every
	// additional instance. A partial final slice intentionally becomes
	// non-semantic and therefore retains identity order.
	if (requiredEffectCount > 1)
	{
		negotiateInstance(firstEffect.get(), effectChannelCount,
			channelNameSlice(0, effectChannelCount));
		if (firstEffect->numInputs() != reportedInputCount
			|| firstEffect->numOutputs() != reportedOutputCount
			|| firstEffect->getInitialDelay() != reportedLatency)
		{
			LogF(L"The VST plugin %s changed metadata while configuring its first split instance; passing audio through.",
				libPath.c_str());
			skipProcessing = true;
			return channelNames;
		}
	}

	effects.reserve(requiredEffectCount);
	effects.push_back(std::move(firstEffect));
	for (size_t i = 1; i < requiredEffectCount; i++)
	{
		try
		{
			effects.push_back(AlignedMemory::constructUnique<VSTPluginInstance>(library, 2));
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

		// Every additional instance is brought to the same negotiated layout
		// as the first one before the consistency check below.
		negotiateInstance(effects[i].get(), effectChannelCount,
			channelNameSlice(i * effectChannelCount, effectChannelCount));

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
		auto channel = AlignedMemory::allocateArray<double>(maxFrameCount);
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
		{
			LogF(L"The VST plugin %s reported input dimensions that overflow the conversion buffer; passing audio through.",
				libPath.c_str());
			skipProcessing = true;
			return channelNames;
		}

		floatInputs.resize(inputCount);
		floatInputBuffer = AlignedMemory::allocateArray<float>(inputCount * maxFrameCount);
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
		{
			LogF(L"The VST plugin %s reported output dimensions that overflow the conversion buffer; passing audio through.",
				libPath.c_str());
			skipProcessing = true;
			return channelNames;
		}

		floatOutputs.resize(outputCount);
		floatOutputBuffer = AlignedMemory::allocateArray<float>(outputCount * maxFrameCount);
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
			auto buffer = AlignedMemory::allocateArray<double>(delayBufferLength);
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
		delayTempBuffer = AlignedMemory::allocateArray<double>(maxFrameCount);
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
			const std::vector<int>& inputMapping = effect->getVST3InputChannelMapping();
			const std::vector<int>& outputMapping = effect->getVST3OutputChannelMapping();

			// Setup double pointer arrays in VST3 bus-slot order. Each mapping
			// is EAPO slot -> accepted VST3 bus slot; VST2 and invalid VST3
			// mappings use the existing identity order.
			for (unsigned eapoSlot = 0; eapoSlot < effectInputCount; eapoSlot++)
			{
				const int mappedSlot = inputMapping.size() == effectInputCount
					? inputMapping[eapoSlot] : static_cast<int>(eapoSlot);
				const unsigned busSlot = mappedSlot >= 0 && mappedSlot < static_cast<int>(effectInputCount)
					? static_cast<unsigned>(mappedSlot) : eapoSlot;
				if (channelOffset + eapoSlot < channelCount)
					inputArray[busSlot] = input[channelOffset + eapoSlot];
				else
					inputArray[busSlot] = emptyChannels[emptyChannelIndex++].get();
			}

			for (unsigned eapoSlot = 0; eapoSlot < effectOutputCount; eapoSlot++)
			{
				const int mappedSlot = outputMapping.size() == effectOutputCount
					? outputMapping[eapoSlot] : static_cast<int>(eapoSlot);
				const unsigned busSlot = mappedSlot >= 0 && mappedSlot < static_cast<int>(effectOutputCount)
					? static_cast<unsigned>(mappedSlot) : eapoSlot;
				if (channelOffset + eapoSlot < channelCount)
					outputArray[busSlot] = output[channelOffset + eapoSlot];
				else
					outputArray[busSlot] = emptyChannels[emptyChannelIndex++].get();
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
		// Only arm the report here; cleanup() writes it. Two reasons not to log on
		// this thread: Logging opens, writes and closes the log file per line
		// (a file open that a filter driver can stall for milliseconds, on the
		// audio thread, while the stream is already glitching), and the CRT state
		// this handler would re-enter is exactly what the plugin just faulted
		// inside. The fault repeats per block, so a one-shot flag loses nothing.
		reportCrash = false;

		// Audit #250 F032: stop re-entering the plugin that just faulted -
		// every later block takes the pass-through fast path above instead
		// of stepping back into dead code.
		skipProcessing = true;

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

bool VSTPluginFilter::getStereoInput() const
{
	return forceStereoInput;
}

void VSTPluginFilter::cleanup()
{
	// Deferred crash report from process(): the audio thread only clears the flag
	// (see the __except handler). Re-arm it so a re-initialized instance can report
	// a fresh fault; cleanup() runs at the start of initialize() and at teardown.
	if (!reportCrash)
	{
		LogF(L"The VST plugin %s crashed during audio processing.", libPath.c_str());
		reportCrash = true;
	}

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
