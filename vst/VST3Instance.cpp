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
#include "text/WideString.h"
#include "platform/windows/TextEncoding.h"
#include "services/logging/Logging.h"
#include "VSTPluginLibrary.h"
#include "VST3Instance.h"
#include "VST3HostContext.h"
#include "VST3MemoryStream.h"
#include "VST3SpeakerMapping.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/vstspeaker.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

bool VST3Instance::initialize()
{
	vst3HostContext = IPtr<VST3HostContext>::adopt(new VST3HostContext(this));

	const PClassInfo& classInfo = library->getVST3ClassInfo();
	FUID componentId(classInfo.cid);
	TUID componentIid;
	IComponent::iid.toTUID(componentIid);
	IComponent* rawComponent = NULL;
	const tresult componentResult = library->getFactory()->createInstance(
		componentId,
		componentIid,
		(void**)&rawComponent);
	vst3Component = IPtr<IComponent>::adopt(rawComponent);
	if (componentResult != kResultOk || vst3Component == NULL)
	{
		LogF(L"Could not create IComponent instance of VST3 plugin %s.", library->getLibPath().c_str());
		return false;
	}

	vst3Component->setIoMode(kSimple);
	if (vst3Component->initialize(static_cast<IHostApplication*>(vst3HostContext.get())) != kResultOk)
	{
		LogF(L"Could not initialize IComponent of VST3 plugin %s.", library->getLibPath().c_str());
		return false;
	}
	vst3ComponentInitialized = true;

	TUID processorIid;
	IAudioProcessor::iid.toTUID(processorIid);
	IAudioProcessor* rawProcessor = NULL;
	const tresult processorResult = vst3Component->queryInterface(processorIid, (void**)&rawProcessor);
	vst3Processor = IPtr<IAudioProcessor>::adopt(rawProcessor);
	if (processorResult != kResultOk || vst3Processor == NULL)
	{
		LogF(L"VST3 plugin %s does not provide the IAudioProcessor interface.", library->getLibPath().c_str());
		return false;
	}

	// A single-component plug-in exposes IEditController from the same object
	// as IComponent. That object is already initialized above and must not be
	// initialized (or later terminated) a second time, so the query comes
	// first and only a separately created controller gets its own lifecycle.
	{
		TUID controllerIid;
		IEditController::iid.toTUID(controllerIid);
		IEditController* rawController = NULL;
		vst3Component->queryInterface(controllerIid, (void**)&rawController);
		vst3Controller = IPtr<IEditController>::adopt(rawController);
	}
	if (vst3Controller == NULL)
	{
		TUID controllerClassId;
		memset(controllerClassId, 0, sizeof(controllerClassId));
		if (vst3Component->getControllerClassId(controllerClassId) == kResultOk)
		{
			TUID controllerIid;
			IEditController::iid.toTUID(controllerIid);
			IEditController* rawController = NULL;
			if (library->getFactory()->createInstance(controllerClassId, controllerIid, (void**)&rawController) == kResultOk
				&& rawController != NULL)
			{
				vst3Controller = IPtr<IEditController>::adopt(rawController);
				if (vst3Controller->initialize(static_cast<IHostApplication*>(vst3HostContext.get())) == kResultOk)
					vst3ControllerInitializedSeparately = true;
				else
				{
					LogF(L"Could not initialize IEditController of VST3 plugin %s.", library->getLibPath().c_str());
					vst3Controller.reset();
				}
			}
		}
	}
	if (vst3Controller != NULL)
	{
		vst3Controller->setComponentHandler(static_cast<IComponentHandler*>(vst3HostContext.get()));

		auto stream = IPtr<VST3MemoryStream>::adopt(new VST3MemoryStream());
		if (vst3Component->getState(stream.get()) == kResultOk)
		{
			stream->seek(0, IBStream::kIBSeekSet);
			vst3Controller->setComponentState(stream.get());
		}

		// The connection pair only exists between two distinct objects; a
		// single-component plug-in is its own counterpart.
		if (vst3ControllerInitializedSeparately)
		{
			TUID connectionPointIid;
			Steinberg::Vst::IConnectionPoint::iid.toTUID(connectionPointIid);
			Steinberg::Vst::IConnectionPoint* rawComponentConnection = NULL;
			const tresult componentConnectionResult = vst3Component->queryInterface(
				connectionPointIid,
				(void**)&rawComponentConnection);
			vst3ComponentConnection = IPtr<Steinberg::Vst::IConnectionPoint>::adopt(rawComponentConnection);

			Steinberg::Vst::IConnectionPoint* rawControllerConnection = NULL;
			const tresult controllerConnectionResult = vst3Controller->queryInterface(
				connectionPointIid,
				(void**)&rawControllerConnection);
			vst3ControllerConnection = IPtr<Steinberg::Vst::IConnectionPoint>::adopt(rawControllerConnection);

			if (componentConnectionResult == kResultOk
				&& controllerConnectionResult == kResultOk
				&& vst3ComponentConnection != NULL
				&& vst3ControllerConnection != NULL)
			{
				vst3ComponentConnection->connect(vst3ControllerConnection);
				vst3ControllerConnection->connect(vst3ComponentConnection);
			}
		}
	}

	vst3Lifecycle.setInterfaces(vst3Component.get(), vst3Processor.get());
	vst3InputBusCount = max(0, vst3Component->getBusCount(kAudio, kInput));
	vst3OutputBusCount = max(0, vst3Component->getBusCount(kAudio, kOutput));
	configureVST3Buses(2, {});

	vst3SupportsDouble = vst3Processor->canProcessSampleSize(kSample64) == kResultOk;
	doubleProcessing = vst3SupportsDouble;
	if (!vst3SupportsDouble && vst3Processor->canProcessSampleSize(kSample32) != kResultOk)
	{
		LogF(L"VST3 plugin %s supports neither 32-bit nor 64-bit sample processing.", library->getLibPath().c_str());
		return false;
	}

	usedChannelCount = max(numInputs(), numOutputs());

	return true;
}

void VST3Instance::releaseVST3()
{
	automateFunc = nullptr;
	sizeWindowFunc = nullptr;
	stopEditing();
	stopProcessingSafely();

	// From here on nothing the plug-in calls through the host context may
	// reach this instance: not during terminate below, and not later through
	// a reference the plug-in kept (audit #348 TD-48).
	if (vst3HostContext != NULL)
		vst3HostContext->detach();
	if (vst3Controller != NULL)
		vst3Controller->setComponentHandler(NULL);

	if (vst3ComponentConnection != NULL && vst3ControllerConnection != NULL)
	{
		vst3ComponentConnection->disconnect(vst3ControllerConnection);
		vst3ControllerConnection->disconnect(vst3ComponentConnection);
	}
	if (vst3ComponentConnection != NULL)
		vst3ComponentConnection.reset();
	if (vst3ControllerConnection != NULL)
		vst3ControllerConnection.reset();
	vst3Lifecycle.clearInterfaces();
	if (vst3Controller != NULL)
	{
		// A controller obtained from the component object is terminated once,
		// through the component below.
		if (vst3ControllerInitializedSeparately)
			vst3Controller->terminate();
		vst3Controller.reset();
	}
	vst3ControllerInitializedSeparately = false;
	if (vst3Processor != NULL)
		vst3Processor.reset();
	if (vst3Component != NULL)
	{
		if (vst3ComponentInitialized)
			vst3Component->terminate();
		vst3Component.reset();
	}
	vst3ComponentInitialized = false;
	if (vst3HostContext != NULL)
		vst3HostContext.reset();

	vst3InputArrangement = SpeakerArr::kEmpty;
	vst3OutputArrangement = SpeakerArr::kEmpty;
	vst3InputChannelNameHints.clear();
	vst3OutputChannelNameHints.clear();
	inputMapping.clear();
	outputMapping.clear();
}

void VST3Instance::configureVST3Buses(int requestedChannelCount,
	const vector<wstring>& channelNames)
{
	configureVST3Buses(requestedChannelCount, requestedChannelCount,
		channelNames, channelNames);
}

void VST3Instance::configureVST3Buses(int requestedInputChannelCount, int requestedOutputChannelCount,
	const vector<wstring>& inputChannelNames, const vector<wstring>& outputChannelNames)
{
	if (vst3Component == NULL || vst3Processor == NULL)
		return;

	const int inputChannelCount = max(1, requestedInputChannelCount);
	const int outputChannelCount = max(1, requestedOutputChannelCount);
	vst3InputChannelNameHints = inputChannelNames;
	vst3OutputChannelNameHints = outputChannelNames;

	applyVST3BusActivation();

	// Semantic proposals are attempted first. Count-based proposals remain
	// available afterwards for plugins that reject the semantic arrangement.
	bool accepted = false;
	const vector<SpeakerArrangement> inputCandidates =
		vst3speakers::arrangementCandidatesForChannelCount(inputChannelCount, inputChannelNames);
	const vector<SpeakerArrangement> outputCandidates =
		vst3speakers::arrangementCandidatesForChannelCount(outputChannelCount, outputChannelNames);
	for (SpeakerArrangement inputCandidate : inputCandidates)
	{
		for (SpeakerArrangement outputCandidate : outputCandidates)
		{
			if (accepted)
				break;
			SpeakerArrangement inputArrangement = inputCandidate;
			SpeakerArrangement outputArrangement = outputCandidate;
			const tresult result = vst3Processor->setBusArrangements(
				vst3InputBusCount > 0 ? &inputArrangement : NULL, vst3InputBusCount > 0 ? 1 : 0,
				vst3OutputBusCount > 0 ? &outputArrangement : NULL, vst3OutputBusCount > 0 ? 1 : 0);
			if (result == kResultTrue)
			{
				const bool arrangementsAvailable = refreshAcceptedVST3Arrangements();
				accepted = arrangementsAvailable
					&& (vst3InputBusCount == 0
						|| SpeakerArr::getChannelCount(vst3InputArrangement) == inputChannelCount)
					&& (vst3OutputBusCount == 0
						|| SpeakerArr::getChannelCount(vst3OutputArrangement) == outputChannelCount);
			}
		}
	}

	if (!accepted)
	{
		// The plugin took none of the proposals (or the width has no standard
		// arrangement). Fall back to the plugin's own preference and re-apply
		// it so both sides agree on one layout, asymmetric buses included.
		SpeakerArrangement inputArrangement = SpeakerArr::kEmpty;
		SpeakerArrangement outputArrangement = SpeakerArr::kEmpty;
		if (vst3InputBusCount > 0)
			vst3Processor->getBusArrangement(kInput, 0, inputArrangement);
		if (vst3OutputBusCount > 0)
			vst3Processor->getBusArrangement(kOutput, 0, outputArrangement);
		if (inputArrangement != SpeakerArr::kEmpty || outputArrangement != SpeakerArr::kEmpty)
		{
			const tresult result = vst3Processor->setBusArrangements(
				vst3InputBusCount > 0 && inputArrangement != SpeakerArr::kEmpty ? &inputArrangement : NULL,
				vst3InputBusCount > 0 && inputArrangement != SpeakerArr::kEmpty ? 1 : 0,
				vst3OutputBusCount > 0 && outputArrangement != SpeakerArr::kEmpty ? &outputArrangement : NULL,
				vst3OutputBusCount > 0 && outputArrangement != SpeakerArr::kEmpty ? 1 : 0);
			if (result == kResultTrue)
				refreshAcceptedVST3Arrangements();
		}
	}

	// Always finish from the arrangements the plugin currently reports. A
	// successful setBusArrangements call does not prove it retained the masks
	// that were proposed.
	refreshAcceptedVST3Arrangements();

	if (vst3InputBusCount > 0)
	{
		vst3InputChannelCount = vst3InputArrangement != SpeakerArr::kEmpty
			? SpeakerArr::getChannelCount(vst3InputArrangement) : vst3BusChannelCount(kInput);
	}
	else
		vst3InputChannelCount = 0;

	if (vst3OutputBusCount > 0)
	{
		vst3OutputChannelCount = vst3OutputArrangement != SpeakerArr::kEmpty
			? SpeakerArr::getChannelCount(vst3OutputArrangement) : vst3BusChannelCount(kOutput);
	}
	else
		vst3OutputChannelCount = 0;

	updateVST3ChannelMappings();
}

void VST3Instance::applyVST3BusActivation()
{
	for (int i = 0; i < vst3InputBusCount; i++)
		vst3Component->activateBus(kAudio, kInput, i, i == 0);
	for (int i = 0; i < vst3OutputBusCount; i++)
		vst3Component->activateBus(kAudio, kOutput, i, i == 0);
}

bool VST3Instance::refreshAcceptedVST3Arrangements()
{
	SpeakerArrangement inputArrangement = SpeakerArr::kEmpty;
	SpeakerArrangement outputArrangement = SpeakerArr::kEmpty;
	bool available = true;

	if (vst3InputBusCount > 0
		&& vst3Processor->getBusArrangement(kInput, 0, inputArrangement) != kResultOk)
		available = false;
	if (vst3OutputBusCount > 0
		&& vst3Processor->getBusArrangement(kOutput, 0, outputArrangement) != kResultOk)
		available = false;

	vst3InputArrangement = inputArrangement;
	vst3OutputArrangement = outputArrangement;
	updateVST3ChannelMappings();
	return available;
}

void VST3Instance::updateVST3ChannelMappings()
{
	vst3speakers::buildChannelMapping(
		vst3InputArrangement, vst3InputChannelNameHints, inputMapping);
	vst3speakers::buildChannelMapping(
		vst3OutputArrangement, vst3OutputChannelNameHints, outputMapping);
}

int VST3Instance::vst3BusChannelCount(BusDirection direction) const
{
	BusInfo busInfo;
	memset(&busInfo, 0, sizeof(busInfo));
	if (vst3Component->getBusInfo(kAudio, direction, 0, busInfo) != kResultOk)
		return 0;
	return max(0, busInfo.channelCount);
}

bool VST3Instance::negotiateChannelCount(int channelCount,
	const vector<wstring>& channelNames)
{
	if (vst3Component == NULL || vst3Processor == NULL)
		return false;

	configureVST3Buses(channelCount, channelNames);
	return max(vst3InputChannelCount, vst3OutputChannelCount) >= channelCount;
}

bool VST3Instance::negotiateBusChannelCounts(int inputChannelCount, int outputChannelCount,
	const vector<wstring>& inputChannelNames, const vector<wstring>& outputChannelNames)
{
	if (vst3Component == NULL || vst3Processor == NULL)
		return false;

	configureVST3Buses(inputChannelCount, outputChannelCount,
		inputChannelNames, outputChannelNames);
	return vst3InputChannelCount == inputChannelCount && vst3OutputChannelCount == outputChannelCount;
}

bool VST3Instance::negotiateBusLayouts(VST3BusLayout inputLayout, VST3BusLayout outputLayout,
	int automaticChannelCount, const vector<wstring>& inputChannelNames,
	const vector<wstring>& outputChannelNames)
{
	if (vst3Component == NULL || vst3Processor == NULL)
		return false;
	if (vst3InputBusCount <= 0 || vst3OutputBusCount <= 0)
		return false;
	vst3InputChannelNameHints = inputChannelNames;
	vst3OutputChannelNameHints = outputChannelNames;

	// Auto/Auto is intentionally the existing automatic path. The explicit contract adds a
	// metadata consistency check, but does not change the proposal or fallback
	// order users already get from VSTPlugin.
	if (inputLayout == VST3BusLayout::Auto && outputLayout == VST3BusLayout::Auto)
	{
		configureVST3Buses(automaticChannelCount, automaticChannelCount,
			inputChannelNames, outputChannelNames);
		return acceptedVST3BusMetadataIsConsistent();
	}

	applyVST3BusActivation();
	refreshAcceptedVST3Arrangements();
	const SpeakerArrangement currentInput = vst3InputArrangement;
	const SpeakerArrangement currentOutput = vst3OutputArrangement;

	const vector<SpeakerArrangement> inputCandidates = vst3speakers::arrangementCandidatesForLayout(
		inputLayout, automaticChannelCount, inputChannelNames, currentInput);
	const vector<SpeakerArrangement> outputCandidates = vst3speakers::arrangementCandidatesForLayout(
		outputLayout, automaticChannelCount, outputChannelNames, currentOutput);

	for (SpeakerArrangement inputCandidate : inputCandidates)
	{
		for (SpeakerArrangement outputCandidate : outputCandidates)
		{
			SpeakerArrangement inputArrangement = inputCandidate;
			SpeakerArrangement outputArrangement = outputCandidate;
			if (vst3Processor->setBusArrangements(&inputArrangement, 1,
				&outputArrangement, 1) != kResultTrue)
				continue;
			if (!refreshAcceptedVST3Arrangements())
				continue;

			const bool inputAccepted = inputLayout == VST3BusLayout::Auto
				? vst3InputArrangement != SpeakerArr::kEmpty
				: vst3speakers::arrangementMatchesLayout(vst3InputArrangement, inputLayout);
			const bool outputAccepted = outputLayout == VST3BusLayout::Auto
				? vst3OutputArrangement != SpeakerArr::kEmpty
				: vst3speakers::arrangementMatchesLayout(vst3OutputArrangement, outputLayout);
			if (!inputAccepted || !outputAccepted || !acceptedVST3BusMetadataIsConsistent())
				continue;

			vst3InputChannelCount = SpeakerArr::getChannelCount(vst3InputArrangement);
			vst3OutputChannelCount = SpeakerArr::getChannelCount(vst3OutputArrangement);
			updateVST3ChannelMappings();
			return true;
		}
	}

	// Preserve the plug-in's final reported state for safe teardown and
	// diagnostics, but never re-apply a different preferred layout here. The
	// caller discards this instance and turns the filter into passthrough.
	refreshAcceptedVST3Arrangements();
	return false;
}

bool VST3Instance::acceptedVST3BusMetadataIsConsistent() const
{
	if (vst3InputArrangement == SpeakerArr::kEmpty || vst3OutputArrangement == SpeakerArr::kEmpty)
		return false;
	const int inputArrangementChannels = SpeakerArr::getChannelCount(vst3InputArrangement);
	const int outputArrangementChannels = SpeakerArr::getChannelCount(vst3OutputArrangement);
	return inputArrangementChannels > 0 && outputArrangementChannels > 0
		&& vst3BusChannelCount(kInput) == inputArrangementChannels
		&& vst3BusChannelCount(kOutput) == outputArrangementChannels;
}

std::optional<VST3BusLayout> VST3Instance::negotiatedInputLayout() const
{
	return vst3speakers::layoutOfArrangement(vst3InputArrangement);
}

std::optional<VST3BusLayout> VST3Instance::negotiatedOutputLayout() const
{
	return vst3speakers::layoutOfArrangement(vst3OutputArrangement);
}

int VST3Instance::numInputs() const
{
	return vst3InputChannelCount;
}

int VST3Instance::numOutputs() const
{
	return vst3OutputChannelCount;
}

int VST3Instance::uniqueID() const
{
	const PClassInfo& classInfo = library->getVST3ClassInfo();
	int result = 0;
	memcpy(&result, classInfo.cid, sizeof(result));
	return result;
}

std::wstring VST3Instance::getName() const
{
	return wintext::toWideString(library->getVST3ClassInfo().name, CP_UTF8);
}

int VST3Instance::getInitialDelay() const
{
	return vst3Processor != NULL ? (int)vst3Processor->getLatencySamples() : 0;
}
