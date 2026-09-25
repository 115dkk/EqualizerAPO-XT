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
#include "audio/ChannelLayout.h"
#include "dsp/SampleConversion.h"
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

std::vector<std::wstring> channelNameSlice(const std::vector<std::wstring>& channelNames, size_t offset, size_t width)
{
	const size_t end = (std::min)(channelNames.size(), offset + width);
	if (offset >= end)
		return std::vector<std::wstring>();
	return std::vector<std::wstring>(channelNames.begin() + offset, channelNames.begin() + end);
}
}

VSTPluginFilter::VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData, const std::unordered_map<std::wstring, float>& paramMap,
	bool stereoInput)
	: library(library), libPath(library->getLibPath()), chunkData(chunkData), paramMap(paramMap), forceStereoInput(stereoInput)
{
}

VSTPluginFilter::VSTPluginFilter(std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData,
	const std::unordered_map<std::wstring, float>& paramMap, VST3BusContract busContract,
	std::vector<std::wstring> inputChannels, std::vector<std::wstring> outputChannels)
	: library(library), libPath(library->getLibPath()), chunkData(chunkData), paramMap(paramMap),
	busContract(busContract), inputChannels(std::move(inputChannels)), outputChannels(std::move(outputChannels))
{
}

VSTPluginFilter::~VSTPluginFilter()
{
	cleanup();
}

template<class... Args>
bool VSTPluginFilter::passThrough(const wchar_t* format, Args... args)
{
	LogF(format, libPath.c_str(), args...);
	skipProcessing = true;
	return false;
}

std::vector<std::wstring> VSTPluginFilter::initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	cleanup();

	channelCount = channelNames.size();
	if (channelCount == 0)
		return channelNames;
	if (channelCount > (std::numeric_limits<unsigned>::max)())
	{
		passThrough(L"The VST plugin %s was assigned too many host channels; passing audio through.");
		return channelNames;
	}

	skipProcessing = false;

	InitContext context;
	if (!createFirstInstance(context, channelNames)
		|| !readMetadata(context)
		|| !planChannels(context, channelNames)
		|| !createRemainingInstances(context, channelNames))
		return channelNames;

	prepareForProcessing(sampleRate, maxFrameCount);
	if (skipProcessing)
		return channelNames;

	if (!allocateBuffers(context, maxFrameCount))
		return channelNames;
	allocateDelayCompensation(context, maxFrameCount);
	return channelNames;
}

bool VSTPluginFilter::negotiateInstance(VSTPluginInstance* effect, unsigned targetChannelCount,
	const std::vector<std::wstring>& outputChannelNames, bool upmixerLayout)
{
	if (busContract)
	{
		const std::vector<std::wstring> inputNames = busContract->input == VST3BusLayout::Auto
			? outputChannelNames : vst3BusLayoutChannelNames(busContract->input);
		const std::vector<std::wstring> contractOutputNames = busContract->output == VST3BusLayout::Auto
			? outputChannelNames : vst3BusLayoutChannelNames(busContract->output);
		effect->setBusChannelNameHints(inputNames, contractOutputNames);
		return effect->negotiateBusLayouts(busContract->input, busContract->output,
			static_cast<int>(targetChannelCount));
	}

	effect->setChannelNameHints(outputChannelNames);
	effect->negotiateChannelCount(static_cast<int>(targetChannelCount));
	if (upmixerLayout && targetChannelCount > 2)
	{
		const std::vector<std::wstring> stereoInputNames = {L"L", L"R"};
		effect->setBusChannelNameHints(stereoInputNames, outputChannelNames);
		effect->negotiateBusChannelCounts(2, static_cast<int>(targetChannelCount));
	}
	return true;
}

bool VSTPluginFilter::createFirstInstance(InitContext& context, const std::vector<std::wstring>& channelNames)
{
	try
	{
		context.firstEffect = AlignedMemory::constructUnique<VSTPluginInstance>(library, 2);
	}
	catch (const std::bad_alloc&)
	{
		return passThrough(L"The VST plugin %s could not allocate its host instance; passing audio through.");
	}
	if (!context.firstEffect->initialize())
		return passThrough(L"The VST plugin %s crashed during initialization.");

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
	context.upmixerLayout = !busContract && channelCount > 2
		&& (forceStereoInput || isUpmixerSubCategory(library->getVST3SubCategories()));
	if (channelCount <= kMaxPluginChannelCount
		&& !negotiateInstance(context.firstEffect.get(), static_cast<unsigned>(channelCount), channelNames,
			context.upmixerLayout))
	{
		return passThrough(L"The VST3 plugin %s does not support the requested %s -> %s bus contract; passing audio through.",
			vst3BusLayoutName(busContract->input), vst3BusLayoutName(busContract->output));
	}
	return true;
}

bool VSTPluginFilter::readMetadata(InitContext& context)
{
	// Metadata is plugin-controlled. Snapshot it once, validate the signed
	// values, and use only the cached values for every allocation and processing
	// loop below. Re-reading allows a broken plugin to change the loop bounds
	// after the corresponding buffers were sized.
	context.reportedInputCount = context.firstEffect->numInputs();
	context.reportedOutputCount = context.firstEffect->numOutputs();
	context.reportedLatency = context.firstEffect->getInitialDelay();
	if (context.reportedInputCount < 0 || context.reportedOutputCount < 0 || context.reportedLatency < 0
		|| context.reportedInputCount > static_cast<int>(kMaxPluginChannelCount)
		|| context.reportedOutputCount > static_cast<int>(kMaxPluginChannelCount)
		|| context.reportedLatency > static_cast<int>(kMaxPluginLatencySamples))
	{
		return passThrough(L"The VST plugin %s reported invalid channel or latency metadata; passing audio through.");
	}

	effectInputCount = static_cast<unsigned>(context.reportedInputCount);
	effectOutputCount = static_cast<unsigned>(context.reportedOutputCount);
	return true;
}

bool VSTPluginFilter::planChannels(InitContext& context, const std::vector<std::wstring>& channelNames)
{
	VSTChannelPlanRequest request;
	request.channelNames = channelNames;
	request.effectInputCount = effectInputCount;
	request.effectOutputCount = effectOutputCount;
	request.oneContractInstance = busContract && busContract->hasExplicitLayout();
	request.inputFill = inputChannels;
	request.outputFill = outputChannels;
	context.plan = planVstChannels(request);
	effectChannelCount = context.plan.effectChannelCount;

	switch (context.plan.refusal)
	{
	case VSTChannelPlan::Refusal::None:
		break;
	case VSTChannelPlan::Refusal::NoChannels:
		skipProcessing = true;
		return false;
	case VSTChannelPlan::Refusal::PaddingOverflow:
		return passThrough(L"The VST plugin %s reported metadata that overflows its padded channel count; passing audio through.");
	case VSTChannelPlan::Refusal::FillNeedsOneInstance:
		return passThrough(L"The VST plugin %s needs several instances, which a channel fill cannot address; passing audio through.");
	case VSTChannelPlan::Refusal::FillSlotCountMismatch:
		return passThrough(L"The VST plugin %s negotiated a different bus slot count than its channel fill names; passing audio through.");
	case VSTChannelPlan::Refusal::FillChannelMissing:
		return passThrough(L"The VST plugin %s has a channel fill naming a channel this device does not have; passing audio through.");
	case VSTChannelPlan::Refusal::DuplicateOutputChannel:
		return passThrough(L"The VST plugin %s has two output slots resolving to the same channel; passing audio through.");
	}

	if (context.plan.usesFill())
	{
		passthroughChannels = context.plan.passthroughChannels;
		resolvedInputChannels = context.plan.resolvedInputChannels;
		resolvedOutputChannels = context.plan.resolvedOutputChannels;
	}
	return true;
}

bool VSTPluginFilter::createRemainingInstances(InitContext& context, const std::vector<std::wstring>& channelNames)
{
	const size_t requiredEffectCount = context.plan.instanceCount;

	// If the full-width proposal fell back to a narrower plugin layout, give
	// the first split instance the same per-instance name slice as every
	// additional instance. A partial final slice intentionally becomes
	// non-semantic and therefore retains identity order.
	if (requiredEffectCount > 1)
	{
		if (!negotiateInstance(context.firstEffect.get(), effectChannelCount,
			channelNameSlice(channelNames, 0, effectChannelCount), context.upmixerLayout))
		{
			return passThrough(L"The VST3 plugin %s rejected its repeated automatic bus layout; passing audio through.");
		}
		if (context.firstEffect->numInputs() != context.reportedInputCount
			|| context.firstEffect->numOutputs() != context.reportedOutputCount
			|| context.firstEffect->getInitialDelay() != context.reportedLatency)
		{
			return passThrough(L"The VST plugin %s changed metadata while configuring its first split instance; passing audio through.");
		}
	}

	effects.reserve(requiredEffectCount);
	effects.push_back(std::move(context.firstEffect));
	for (size_t i = 1; i < requiredEffectCount; i++)
	{
		try
		{
			effects.push_back(AlignedMemory::constructUnique<VSTPluginInstance>(library, 2));
		}
		catch (const std::bad_alloc&)
		{
			return passThrough(L"The VST plugin %s could not allocate instance %Iu; passing audio through.", i);
		}
		if (!effects[i]->initialize())
			return passThrough(L"The VST plugin %s crashed during initialization.");

		// Every additional instance is brought to the same negotiated layout
		// as the first one before the consistency check below.
		if (!negotiateInstance(effects[i].get(), effectChannelCount,
			channelNameSlice(channelNames, i * effectChannelCount, effectChannelCount), context.upmixerLayout))
		{
			return passThrough(L"The VST3 plugin %s rejected an automatic bus layout on instance %Iu; passing audio through.", i);
		}

		const int instanceInputCount = effects[i]->numInputs();
		const int instanceOutputCount = effects[i]->numOutputs();
		const int instanceLatency = effects[i]->getInitialDelay();
		if (instanceInputCount != context.reportedInputCount
			|| instanceOutputCount != context.reportedOutputCount
			|| instanceLatency != context.reportedLatency)
		{
			return passThrough(L"The VST plugin %s reported inconsistent per-instance metadata; passing audio through.");
		}
	}
	return true;
}

bool VSTPluginFilter::allocateBuffers(const InitContext& context, unsigned maxFrameCount)
{
	const size_t paddedChannelCount = context.plan.paddedChannelCount;
	const size_t fillScratchCount = context.plan.fillScratchCount;

	// 2 times for input and output
	const size_t paddingChannelCount = paddedChannelCount > channelCount
		? paddedChannelCount - channelCount : 0;
	if ((!(busContract && busContract->hasExplicitLayout()) && paddedChannelCount < channelCount)
		|| paddingChannelCount > ((std::numeric_limits<size_t>::max)() - fillScratchCount) / 2)
	{
		return passThrough(L"The VST plugin %s reported metadata that overflows its padding count; passing audio through.");
	}
	const size_t emptyChannelCount = 2 * paddingChannelCount + fillScratchCount;
	emptyChannels.reserve(emptyChannelCount);
	for (size_t i = 0; i < emptyChannelCount; i++)
	{
		auto channel = AlignedMemory::allocateArray<double>(maxFrameCount);
		if (!channel)
			return passThrough(L"The VST plugin %s could not allocate padding channel %Iu; passing audio through.", i);
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
			return passThrough(L"The VST plugin %s reported input dimensions that overflow the conversion buffer; passing audio through.");
		}

		floatInputs.resize(inputCount);
		floatInputBuffer = AlignedMemory::allocateArray<float>(inputCount * maxFrameCount);
		if (!floatInputBuffer)
			return passThrough(L"The VST plugin %s could not allocate float input buffers; passing audio through.");
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
			return passThrough(L"The VST plugin %s reported output dimensions that overflow the conversion buffer; passing audio through.");
		}

		floatOutputs.resize(outputCount);
		floatOutputBuffer = AlignedMemory::allocateArray<float>(outputCount * maxFrameCount);
		if (!floatOutputBuffer)
			return passThrough(L"The VST plugin %s could not allocate float output buffers; passing audio through.");
		for (unsigned i = 0; i < effectOutputCount; ++i) {
			floatOutputs[i] = floatOutputBuffer.get() + i * maxFrameCount;
		}
	}
	return true;
}

bool VSTPluginFilter::allocateDelayCompensation(const InitContext& context, unsigned maxFrameCount)
{
	// A channel a plugin output writes is already late by the reported latency.
	// Only the channels the plugin does not write are delayed, so they line up
	// with the processed ones: with a fill, the channels no output slot names;
	// without one, the device channels beyond the instances' buses, which an
	// explicit contract copies through unchanged.
	const unsigned latency = static_cast<unsigned>(context.reportedLatency);
	if (latency == 0)
		return true;
	if (context.plan.usesFill())
		delayedChannels = context.plan.passthroughChannels;
	else
	{
		for (size_t channel = context.plan.paddedChannelCount; channel < channelCount; channel++)
			delayedChannels.push_back(static_cast<unsigned>(channel));
	}
	if (delayedChannels.empty())
		return true;

	delayedOutputs.assign(delayedChannels.size(), nullptr);
	if (!latencyDelay.allocate(static_cast<unsigned>(delayedChannels.size()), latency, maxFrameCount))
		return passThrough(L"The VST plugin %s could not allocate its delay compensation buffers; passing audio through.");
	return true;
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
		const bool inputFill = !resolvedInputChannels.empty();
		const bool outputFill = !resolvedOutputChannels.empty();
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
				if (inputFill)
				{
					const int sourceChannel = resolvedInputChannels[eapoSlot];
					inputArray[busSlot] = sourceChannel >= 0
						? input[sourceChannel] : emptyChannels[emptyChannelIndex++].get();
				}
				else if (channelOffset + eapoSlot < channelCount)
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
				if (outputFill)
				{
					const int targetChannel = resolvedOutputChannels[eapoSlot];
					outputArray[busSlot] = targetChannel >= 0
						? output[targetChannel] : emptyChannels[emptyChannelIndex++].get();
				}
				else if (channelOffset + eapoSlot < channelCount)
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
					sampleconv::demote(floatInputs[j], inputArray[j], frameCount);
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
					sampleconv::promote(outputArray[j], floatOutputs[j], frameCount);
				}
			}

			if (!inputFill && !outputFill && effectOutputCount < effectInputCount)
			{
				for (unsigned j = effectOutputCount; j < effectInputCount; j++)
				{
					if (channelOffset + j < channelCount)
						std::fill_n(output[channelOffset + j], frameCount, 0.0);
				}
			}

			channelOffset += effectChannelCount;
		}

		if (inputFill || outputFill)
		{
			// A fill decides which channels the plugin writes, so every other
			// channel keeps its own signal. Reading a channel into a bus slot
			// does not consume it.
			for (unsigned channel : passthroughChannels)
				std::copy_n(input[channel], frameCount, output[channel]);
		}
		else
		{
			// An explicit downmix or otherwise narrower contract owns one main bus,
			// never repeated instances. Device channels outside that bus stay intact.
			for (unsigned channel = channelOffset; channel < channelCount; channel++)
				std::copy_n(input[channel], frameCount, output[channel]);
		}

		// Apply delay compensation if needed
		if (!latencyDelay.empty())
		{
			for (size_t i = 0; i < delayedChannels.size(); i++)
				delayedOutputs[i] = output[delayedChannels[i]];
			latencyDelay.process(delayedOutputs.data(), delayedOutputs.data(), frameCount);
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

const std::optional<VST3BusContract>& VSTPluginFilter::getBusContract() const
{
	return busContract;
}

const std::vector<std::wstring>& VSTPluginFilter::getInputChannels() const
{
	return inputChannels;
}

const std::vector<std::wstring>& VSTPluginFilter::getOutputChannels() const
{
	return outputChannels;
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

	resolvedInputChannels.clear();
	resolvedOutputChannels.clear();
	passthroughChannels.clear();

	emptyChannels.clear();
	inputArray.clear();
	outputArray.clear();
	floatInputs.clear();
	floatInputBuffer.reset();
	floatOutputs.clear();
	floatOutputBuffer.reset();
	latencyDelay.release();
	delayedChannels.clear();
	delayedOutputs.clear();
}
