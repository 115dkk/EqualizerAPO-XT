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
#include <cmath>
#include <cwctype>
#include <exception>
#include <limits>
#include <new>
#include <malloc.h>
#include "services/logging/Logging.h"
#include "audio/ChannelLayout.h"
#include "engine/LatencyMath.h"
#include "dsp/MxcsrGuard.h"
#include "dsp/SampleConversion.h"
#include "VSTPluginFilter.h"
// After VSTPluginFilter.h: the VST3 SDK defines VST_VERSION, which would
// otherwise replace the VST2 enum of the same name in aeffectx.h.
#include "vst/VST3SpeakerMapping.h"
#include "platform/windows/StackGuarantee.h"

using std::max;

namespace
{
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

std::vector<std::wstring> VSTPluginFilter::getNamedChannels(const std::vector<std::wstring>& selection)
{
	announcedSelectionWidth = selection.size();
	std::vector<std::wstring> named;
	const auto collect = [&selection, &named](const std::vector<std::wstring>& fill) {
		for (const std::wstring& selector : fill)
		{
			// "-" is a silent slot and a number is a position in the selection;
			// neither names a channel the engine could append.
			if (selector.empty() || selector == L"-" || iswdigit(selector[0]))
				continue;
			if (ChannelLayout::getChannelIndex(selector, selection, true) >= 0)
				continue;
			if (std::find(named.begin(), named.end(), selector) == named.end())
				named.push_back(selector);
		}
	};
	collect(inputChannels);
	collect(outputChannels);
	return named;
}

std::vector<std::wstring> VSTPluginFilter::initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	cleanup();

	channelCount = channelNames.size();
	// Without getNamedChannels (a direct caller such as a test), every channel
	// handed in is the selection, as before issue #519.
	selectedChannelCount = announcedSelectionWidth && *announcedSelectionWidth <= channelCount
		? *announcedSelectionWidth : channelCount;
	if (channelCount == 0)
		return channelNames;
	if (channelCount > (std::numeric_limits<unsigned>::max)())
	{
		passThrough(L"The VST plugin %s was assigned too many host channels; passing audio through.");
		return channelNames;
	}

	skipProcessing = false;

	InitContext context;
	if (!createFirstInstance(context, channelNames))
		return channelNames;
	readMetadata(context);
	if (!planChannels(context, channelNames) || !createRemainingInstances(context, channelNames))
		return channelNames;

	processingWorkspace = std::make_shared<VSTPluginInstance::ProcessingWorkspace>();
	prepareForProcessing(sampleRate, maxFrameCount);
	if (skipProcessing)
		return channelNames;

	const int activeLatency = effects.front()->getInitialDelay();
	for (size_t i = 1; i < effects.size(); i++)
	{
		if (effects[i]->getInitialDelay() != activeLatency)
		{
			passThrough(L"The VST plugin %s reported inconsistent per-instance latency after loading its state; passing audio through.");
			return channelNames;
		}
	}
	if (activeLatency != context.reportedLatency)
	{
		LogF(L"The VST plugin %s reports %d samples of latency after loading its state (%d before); compensation uses the new value.",
			libPath.c_str(), activeLatency, context.reportedLatency);
		context.reportedLatency = activeLatency;
	}

	if (!allocateBuffers(context, maxFrameCount))
		return channelNames;
	if (!allocateDelayCompensation(context, maxFrameCount))
		return channelNames;

	reportedLatencyFrames = static_cast<unsigned>(context.reportedLatency);
	pluginWrittenChannels.clear();
	pluginWrittenInputChannels.clear();
	pluginZeroedChannels.clear();
	const auto inputsForInstance = [this, &context](unsigned channelOffset) {
		std::vector<unsigned> result;
		if (!context.plan.resolvedInputChannels.empty())
		{
			for (const int channel : context.plan.resolvedInputChannels)
			{
				if (channel >= 0)
					result.push_back(static_cast<unsigned>(channel));
			}
		}
		else
		{
			for (unsigned slot = 0; slot < effectInputCount
				&& channelOffset + slot < static_cast<unsigned>(selectedChannelCount); ++slot)
			{
				result.push_back(channelOffset + slot);
			}
		}
		return result;
	};
	if (!context.plan.resolvedOutputChannels.empty())
	{
		const std::vector<unsigned> instanceInputs = inputsForInstance(0);
		for (const int channel : context.plan.resolvedOutputChannels)
		{
			if (channel >= 0)
			{
				pluginWrittenChannels.push_back(static_cast<unsigned>(channel));
				pluginWrittenInputChannels.push_back(instanceInputs);
			}
		}
	}
	else
	{
		unsigned channelOffset = 0;
		for (size_t instance = 0; instance < effects.size(); ++instance)
		{
			const std::vector<unsigned> instanceInputs = inputsForInstance(channelOffset);
			for (unsigned slot = 0; slot < effectOutputCount
				&& channelOffset + slot < static_cast<unsigned>(selectedChannelCount); ++slot)
			{
				pluginWrittenChannels.push_back(channelOffset + slot);
				pluginWrittenInputChannels.push_back(instanceInputs);
			}
			if (context.plan.resolvedInputChannels.empty())
			{
				for (unsigned slot = effectOutputCount; slot < effectInputCount
					&& channelOffset + slot < static_cast<unsigned>(selectedChannelCount); ++slot)
				{
					pluginZeroedChannels.push_back(channelOffset + slot);
				}
			}
			channelOffset += effectChannelCount;
		}
	}
	return channelNames;
}

bool VSTPluginFilter::negotiateInstance(VSTPluginInstance* effect, unsigned targetChannelCount,
	const std::vector<std::wstring>& outputChannelNames, bool upmixerLayout)
{
	VSTPluginInstance::NegotiationRequest request;
	request.contract = busContract;
	request.channelCount = static_cast<int>(targetChannelCount);
	request.channelNames = outputChannelNames;
	request.stereoInput = upmixerLayout;
	const auto result = effect->negotiate(request);
	if (!result.accepted)
		LogF(L"The VST plugin %s could not negotiate: %s.", libPath.c_str(), result.reason.c_str());
	return result.accepted;
}

bool VSTPluginFilter::createFirstInstance(InitContext& context, const std::vector<std::wstring>& channelNames)
{
	try
	{
		context.firstEffect = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	}
	catch (const std::bad_alloc&)
	{
		return passThrough(L"The VST plugin %s could not allocate its host instance; passing audio through.");
	}
	try
	{
		if (!context.firstEffect->initialize())
		{
			if (context.firstEffect->initializationFailure() == VSTPluginInstance::InitializationFailure::StructuredException)
				return passThrough(L"The VST plugin %s raised a structured exception during initialization; passing audio through.");
			return passThrough(L"The VST plugin %s refused initialization; passing audio through.");
		}
	}
	catch (const std::exception& e)
	{
		return passThrough(L"The VST plugin %s threw a C++ exception during initialization: %S; passing audio through.", e.what());
	}
	catch (...)
	{
		return passThrough(L"The VST plugin %s threw an exception of unknown type during initialization; passing audio through.");
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
	context.upmixerLayout = forceStereoInput;
	// The selection decides the width of an Auto side; channels appended for
	// the fill (issue #519) are only ever addressed by name.
	if (!negotiateInstance(context.firstEffect.get(), static_cast<unsigned>(selectedChannelCount),
		channelNameSlice(channelNames, 0, selectedChannelCount),
		context.upmixerLayout))
		return passThrough(L"The VST plugin %s could not negotiate its buses; passing audio through.");
	return true;
}

void VSTPluginFilter::readMetadata(InitContext& context)
{
	// Metadata is plugin-controlled. Snapshot it once, validate the signed
	// values, and use only the cached values for every allocation and processing
	// loop below. Re-reading allows a broken plugin to change the loop bounds
	// after the corresponding buffers were sized.
	context.reportedInputCount = context.firstEffect->numInputs();
	context.reportedOutputCount = context.firstEffect->numOutputs();
	context.reportedLatency = context.firstEffect->getInitialDelay();

	effectInputCount = static_cast<unsigned>(context.reportedInputCount);
	effectOutputCount = static_cast<unsigned>(context.reportedOutputCount);
}

bool VSTPluginFilter::planChannels(InitContext& context, const std::vector<std::wstring>& channelNames)
{
	VSTChannelPlanRequest request;
	request.channelNames = channelNames;
	request.selectedChannelCount = selectedChannelCount;
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
		// Issue #519: say which entry, and why it found nothing. A name reaches
		// any channel the device or an earlier line has, selected or not; a
		// number only counts the selection.
		if (!context.plan.missingChannel.empty() && iswdigit(context.plan.missingChannel[0]))
			return passThrough(L"The VST plugin %s has a channel fill naming channel %s, but only %u channels are selected at this line; passing audio through.",
				context.plan.missingChannel.c_str(), static_cast<unsigned>(selectedChannelCount));
		return passThrough(L"The VST plugin %s has a channel fill naming %s, which neither the device nor an earlier line has created; passing audio through.",
			context.plan.missingChannel.c_str());
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
			effects.push_back(VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime));
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
	const size_t paddingChannelCount = paddedChannelCount > selectedChannelCount
		? paddedChannelCount - selectedChannelCount : 0;
	if ((!(busContract && busContract->hasExplicitLayout()) && paddedChannelCount < selectedChannelCount)
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
	// TestVst2Plugin's prepare/state and TestVst3Plugin's FaultActive/FaultState
	// modes raise AVs here. Both testEngineFaultContainment cases require
	// pass-through and a load-time diagnostic (audit #467 TD-28).
	const unsigned __int64 fpControlState = captureFpControlState();
	ensurePluginCallStackGuarantee();
	volatile bool stackOverflow = false;
	__try
	{
		bool restoreFailureReported = false;
		for (size_t i = 0; i < effects.size(); i++)
		{
			VSTPluginInstance* effect = effects[i].get();

			if (i == effects.size() - 1 && (selectedChannelCount % effectChannelCount) != 0)
				effect->setUsedChannelCount(static_cast<int>(selectedChannelCount % effectChannelCount));
			else
				effect->setUsedChannelCount(effectChannelCount);
			if (!effect->prepareForProcessing(sampleRate, maxFrameCount,
				VSTPluginInstance::InputWidth::Double, processingWorkspace))
			{
				passThrough(L"The VST plugin %s could not prepare for processing; passing audio through.");
				restoreFpControlState(fpControlState);
				return;
			}
			const auto restored = effect->writeToEffect(chunkData, paramMap);
			if (!restored && !restoreFailureReported)
			{
				// Split instances restore the same line; testStateRestoreFailureIsReported
				// requires one load-time report, never a per-block warning (audit #467 TD-36).
				LogF(L"The VST plugin %s could not restore its state: %s.", libPath.c_str(), restored.reason);
				restoreFailureReported = true;
			}
			if (!effect->startProcessing())
			{
				passThrough(L"The VST plugin %s could not start processing; passing audio through.");
				restoreFpControlState(fpControlState);
				return;
			}
		}
		restoreFpControlState(fpControlState);
	}
	__except ((stackOverflow = GetExceptionCode() == EXCEPTION_STACK_OVERFLOW)
		? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_EXECUTE_HANDLER)
	{
		restoreFpControlState(fpControlState);
		LogF(L"The VST plugin %s crashed while preparing for processing.", libPath.c_str());
		skipProcessing = true;
	}
	// A caught overflow consumes the thread's guard page; re-arm it after SEH unwinds.
	if (stackOverflow)
		_resetstkoflw();
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

	const unsigned __int64 fpControlState = captureFpControlState();
	ensurePluginCallStackGuarantee();
	volatile bool stackOverflow = false;
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
				else if (channelOffset + eapoSlot < selectedChannelCount)
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
				else if (channelOffset + eapoSlot < selectedChannelCount)
					outputArray[busSlot] = output[channelOffset + eapoSlot];
				else
					outputArray[busSlot] = emptyChannels[emptyChannelIndex++].get();
			}

			effect->processBlock(inputArray.data(), outputArray.data(), frameCount);
			restoreFpControlState(fpControlState);

			// Non-finite output guard, adapted from Custom Audio Manager by weirdsoup (MIT): see Third-party notices.
			bool nonFiniteOutput = false;
			for (unsigned eapoSlot = 0; eapoSlot < effectOutputCount; eapoSlot++)
			{
				const bool realOutput = outputFill
					? resolvedOutputChannels[eapoSlot] >= 0 : channelOffset + eapoSlot < selectedChannelCount;
				if (!realOutput)
					continue;
				const int mappedSlot = outputMapping.size() == effectOutputCount
					? outputMapping[eapoSlot] : static_cast<int>(eapoSlot);
				const unsigned busSlot = mappedSlot >= 0 && mappedSlot < static_cast<int>(effectOutputCount)
					? static_cast<unsigned>(mappedSlot) : eapoSlot;
				const double* samples = outputArray[busSlot];
				for (unsigned frame = 0; frame < frameCount; frame++)
					nonFiniteOutput |= !std::isfinite(samples[frame]);
			}
			if (nonFiniteOutput)
			{
				reportNonFinite = false;
				skipProcessing = true;
				for (unsigned channel = 0; channel < channelCount; channel++)
					std::copy_n(input[channel], frameCount, output[channel]);
				return;
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
	__except ((stackOverflow = GetExceptionCode() == EXCEPTION_STACK_OVERFLOW)
		? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_EXECUTE_HANDLER)
	{
		restoreFpControlState(fpControlState);
		// TestVst2Plugin's process and TestVst3Plugin's FaultProcess modes raise
		// AVs here; both testEngineFaultContainment cases verify this block,
		// later blocks, and the one deferred report (audit #467 TD-28).
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
	// A caught overflow consumes the thread's guard page; re-arm it after SEH unwinds.
	if (stackOverflow)
		_resetstkoflw();
}
#pragma AVRT_CODE_END

void VSTPluginFilter::reportLatency(const std::vector<unsigned>& input,
	std::vector<unsigned>& output) const
{
	if (skipProcessing)
		return;

	const size_t writtenCount = (std::min)(pluginWrittenChannels.size(),
		pluginWrittenInputChannels.size());
	for (size_t written = 0; written < writtenCount; ++written)
	{
		const unsigned outputChannel = pluginWrittenChannels[written];
		if (outputChannel >= output.size())
			continue;
		unsigned largestInput = 0;
		for (const unsigned inputChannel : pluginWrittenInputChannels[written])
		{
			if (inputChannel < input.size())
				largestInput = (std::max)(largestInput, input[inputChannel]);
		}
		output[outputChannel] = addLatency(largestInput, reportedLatencyFrames);
	}
	for (const unsigned channel : pluginZeroedChannels)
	{
		if (channel < output.size())
			output[channel] = 0;
	}
	if (!latencyDelay.empty())
	{
		for (const unsigned channel : delayedChannels)
		{
			if (channel < input.size() && channel < output.size())
				output[channel] = addLatency(input[channel], latencyDelay.length());
		}
	}
}

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
	if (!reportNonFinite)
	{
		LogF(L"The VST plugin %s produced NaN or infinite samples; passing audio through.", libPath.c_str());
		reportNonFinite = true;
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
	processingWorkspace.reset();
	latencyDelay.release();
	delayedChannels.clear();
	delayedOutputs.clear();
	pluginWrittenChannels.clear();
	pluginWrittenInputChannels.clear();
	pluginZeroedChannels.clear();
	reportedLatencyFrames = 0;
}
