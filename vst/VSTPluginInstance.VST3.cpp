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
#include "services/logging/LogHelper.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "VSTPluginInstanceInternal.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/vstspeaker.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
bool channelNamesEqual(const vector<wstring>& channelNames, initializer_list<const wchar_t*> expected)
{
	if (channelNames.size() != expected.size())
		return false;

	size_t index = 0;
	for (const wchar_t* name : expected)
	{
		if (channelNames[index++] != name)
			return false;
	}
	return true;
}

bool appendArrangementCandidate(SpeakerArrangement arrangement, SpeakerArrangement* candidates, int& count)
{
	for (int i = 0; i < count; i++)
	{
		if (candidates[i] == arrangement)
			return false;
	}
	candidates[count++] = arrangement;
	return true;
}
}

bool VSTPluginInstance::initializeVST3()
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

	vst3InputBusCount = max(0, vst3Component->getBusCount(kAudio, kInput));
	vst3OutputBusCount = max(0, vst3Component->getBusCount(kAudio, kOutput));
	configureVST3Buses(2);

	vst3SupportsDouble = vst3Processor->canProcessSampleSize(kSample64) == kResultOk;
	if (!vst3SupportsDouble && vst3Processor->canProcessSampleSize(kSample32) != kResultOk)
	{
		LogF(L"VST3 plugin %s supports neither 32-bit nor 64-bit sample processing.", library->getLibPath().c_str());
		return false;
	}

	usedChannelCount = max(numInputs(), numOutputs());

	return true;
}

void VSTPluginInstance::releaseVST3()
{
	if (!library->isVST3())
		return;

	automateFunc = nullptr;
	sizeWindowFunc = nullptr;
	stopEditing();
	stopProcessingSafely();

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
	vst3InputChannelMapping.clear();
	vst3OutputChannelMapping.clear();
}

void VSTPluginInstance::setChannelNameHints(const vector<wstring>& channelNames)
{
	setBusChannelNameHints(channelNames, channelNames);
}

void VSTPluginInstance::setBusChannelNameHints(const vector<wstring>& inputChannelNames,
	const vector<wstring>& outputChannelNames)
{
	vst3InputChannelNameHints = inputChannelNames;
	vst3OutputChannelNameHints = outputChannelNames;
	updateVST3ChannelMappings();
}

void VSTPluginInstance::configureVST3Buses(int requestedChannelCount)
{
	configureVST3Buses(requestedChannelCount, requestedChannelCount);
}

void VSTPluginInstance::configureVST3Buses(int requestedInputChannelCount, int requestedOutputChannelCount)
{
	if (vst3Component == NULL || vst3Processor == NULL)
		return;

	const int inputChannelCount = max(1, requestedInputChannelCount);
	const int outputChannelCount = max(1, requestedOutputChannelCount);

	applyVST3BusActivation();

	// Semantic proposals are attempted first. Count-based proposals remain
	// available afterwards for plugins that reject the semantic arrangement.
	bool accepted = false;
	SpeakerArrangement inputCandidates[vst3MaxArrangementCandidates];
	SpeakerArrangement outputCandidates[vst3MaxArrangementCandidates];
	const int inputCandidateCount = speakerArrangementCandidatesForChannelCount(
		inputChannelCount, vst3InputChannelNameHints, inputCandidates);
	const int outputCandidateCount = speakerArrangementCandidatesForChannelCount(
		outputChannelCount, vst3OutputChannelNameHints, outputCandidates);
	for (int i = 0; i < inputCandidateCount && !accepted; i++)
	{
		for (int j = 0; j < outputCandidateCount && !accepted; j++)
		{
			SpeakerArrangement inputArrangement = inputCandidates[i];
			SpeakerArrangement outputArrangement = outputCandidates[j];
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

void VSTPluginInstance::applyVST3BusActivation()
{
	for (int i = 0; i < vst3InputBusCount; i++)
		vst3Component->activateBus(kAudio, kInput, i, i == 0);
	for (int i = 0; i < vst3OutputBusCount; i++)
		vst3Component->activateBus(kAudio, kOutput, i, i == 0);
}

bool VSTPluginInstance::refreshAcceptedVST3Arrangements()
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

void VSTPluginInstance::updateVST3ChannelMappings()
{
	buildVST3ChannelMapping(vst3InputArrangement, vst3InputChannelNameHints, vst3InputChannelMapping);
	buildVST3ChannelMapping(vst3OutputArrangement, vst3OutputChannelNameHints, vst3OutputChannelMapping);
}

bool VSTPluginInstance::buildVST3ChannelMapping(SpeakerArrangement arrangement,
	const vector<wstring>& channelNames, vector<int>& mapping) const
{
	const int channelCount = arrangement != SpeakerArr::kEmpty
		? SpeakerArr::getChannelCount(arrangement) : 0;
	mapping.resize(max(0, channelCount));
	for (int i = 0; i < channelCount; i++)
		mapping[i] = i;

	if (channelCount <= 0 || channelNames.size() != static_cast<size_t>(channelCount))
		return false;

	SpeakerArrangement semanticCandidates[vst3MaxArrangementCandidates];
	const int semanticCandidateCount = semanticSpeakerArrangementCandidatesForChannelNames(
		channelNames, semanticCandidates);
	bool knownArrangement = false;
	for (int i = 0; i < semanticCandidateCount; i++)
	{
		if (semanticCandidates[i] == arrangement)
		{
			knownArrangement = true;
			break;
		}
	}
	if (!knownArrangement)
		return false;

	const bool hasRearPair = find(channelNames.begin(), channelNames.end(), L"RL") != channelNames.end()
		&& find(channelNames.begin(), channelNames.end(), L"RR") != channelNames.end();
	const bool hasSidePair = find(channelNames.begin(), channelNames.end(), L"SL") != channelNames.end()
		&& find(channelNames.begin(), channelNames.end(), L"SR") != channelNames.end();

	vector<bool> usedBusSlots(channelCount, false);
	for (int eapoSlot = 0; eapoSlot < channelCount; eapoSlot++)
	{
		const wstring& name = channelNames[eapoSlot];
		Speaker speaker = 0;
		if (name == L"L")
			speaker = kSpeakerL;
		else if (name == L"R")
			speaker = kSpeakerR;
		else if (name == L"C")
			speaker = kSpeakerC;
		else if (name == L"LFE")
			speaker = kSpeakerLfe;
		else if (name == L"RL")
			speaker = kSpeakerLs;
		else if (name == L"RR")
			speaker = kSpeakerRs;
		else if (name == L"SL")
			speaker = hasRearPair && hasSidePair ? kSpeakerSl : kSpeakerLs;
		else if (name == L"SR")
			speaker = hasRearPair && hasSidePair ? kSpeakerSr : kSpeakerRs;
		else
			return false;

		const int busSlot = SpeakerArr::getSpeakerIndex(speaker, arrangement);
		if (busSlot < 0 || busSlot >= channelCount || usedBusSlots[busSlot])
			return false;
		mapping[eapoSlot] = busSlot;
		usedBusSlots[busSlot] = true;
	}

	for (bool used : usedBusSlots)
	{
		if (!used)
			return false;
	}
	return true;
}

int VSTPluginInstance::vst3BusChannelCount(BusDirection direction) const
{
	BusInfo busInfo;
	memset(&busInfo, 0, sizeof(busInfo));
	if (vst3Component->getBusInfo(kAudio, direction, 0, busInfo) != kResultOk)
		return 0;
	return max(0, busInfo.channelCount);
}

bool VSTPluginInstance::negotiateChannelCount(int channelCount)
{
	if (!library->isVST3())
		return max(numInputs(), numOutputs()) >= channelCount;
	if (vst3Component == NULL || vst3Processor == NULL)
		return false;

	configureVST3Buses(channelCount);
	return max(vst3InputChannelCount, vst3OutputChannelCount) >= channelCount;
}

bool VSTPluginInstance::negotiateBusChannelCounts(int inputChannelCount, int outputChannelCount)
{
	if (!library->isVST3())
		return numInputs() >= inputChannelCount && numOutputs() >= outputChannelCount;
	if (vst3Component == NULL || vst3Processor == NULL)
		return false;

	configureVST3Buses(inputChannelCount, outputChannelCount);
	return vst3InputChannelCount == inputChannelCount && vst3OutputChannelCount == outputChannelCount;
}

int VSTPluginInstance::semanticSpeakerArrangementCandidatesForChannelNames(
	const vector<wstring>& channelNames, SpeakerArrangement* candidates) const
{
	if (channelNamesEqual(channelNames, {L"L", L"R"}))
	{
		candidates[0] = SpeakerArr::kStereo;
		return 1;
	}
	if (channelNamesEqual(channelNames, {L"L", L"R", L"LFE", L"RL", L"RR"})
		|| channelNamesEqual(channelNames, {L"L", L"R", L"LFE", L"SL", L"SR"}))
	{
		candidates[0] = SpeakerArr::k41Music;
		return 1;
	}
	if (channelNamesEqual(channelNames, {L"L", L"R", L"C", L"RL", L"RR"})
		|| channelNamesEqual(channelNames, {L"L", L"R", L"C", L"SL", L"SR"}))
	{
		candidates[0] = SpeakerArr::k50;
		return 1;
	}
	if (channelNamesEqual(channelNames, {L"L", L"R", L"C", L"LFE", L"RL", L"RR"}))
	{
		candidates[0] = SpeakerArr::k51;
		return 1;
	}
	if (channelNamesEqual(channelNames, {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"}))
	{
		candidates[0] = SpeakerArr::k71Music;
		candidates[1] = SpeakerArr::k71Cine;
		return 2;
	}
	return 0;
}

int VSTPluginInstance::speakerArrangementCandidatesForChannelCount(int count,
	const vector<wstring>& channelNames, SpeakerArrangement* candidates) const
{
	int candidateCount = semanticSpeakerArrangementCandidatesForChannelNames(channelNames, candidates);

	// Count-based candidates stay after semantic candidates and preserve the
	// existing Windows-mask-first ordering.
	switch (count)
	{
	case 1:
		appendArrangementCandidate(SpeakerArr::kMono, candidates, candidateCount);
		break;
	case 2:
		appendArrangementCandidate(SpeakerArr::kStereo, candidates, candidateCount);
		break;
	case 4:
		appendArrangementCandidate(SpeakerArr::k40Music, candidates, candidateCount);
		appendArrangementCandidate(SpeakerArr::k40Cine, candidates, candidateCount);
		break;
	case 5:
		appendArrangementCandidate(SpeakerArr::k50, candidates, candidateCount);
		break;
	case 6:
		appendArrangementCandidate(SpeakerArr::k51, candidates, candidateCount);
		break;
	case 7:
		appendArrangementCandidate(SpeakerArr::k61Cine, candidates, candidateCount);
		break;
	case 8:
		appendArrangementCandidate(SpeakerArr::k71Music, candidates, candidateCount);
		appendArrangementCandidate(SpeakerArr::k71Cine, candidates, candidateCount);
		break;
	case 10:
		appendArrangementCandidate(SpeakerArr::k71_2, candidates, candidateCount);
		break;
	case 12:
		appendArrangementCandidate(SpeakerArr::k71_4, candidates, candidateCount);
		break;
	}
	return candidateCount;
}
