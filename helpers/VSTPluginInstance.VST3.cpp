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
#include "LogHelper.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "VSTPluginInstanceInternal.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/base/smartpointer.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

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

	// Propose arrangements matching the requested widths and verify each
	// attempt: the return value alone is not enough because the bus info the
	// component reports afterwards is what the process() buffers must match.
	bool accepted = false;
	SpeakerArrangement inputCandidates[vst3MaxArrangementCandidates];
	SpeakerArrangement outputCandidates[vst3MaxArrangementCandidates];
	const int inputCandidateCount = speakerArrangementCandidatesForChannelCount(inputChannelCount, inputCandidates);
	const int outputCandidateCount = speakerArrangementCandidatesForChannelCount(outputChannelCount, outputCandidates);
	for (int i = 0; i < inputCandidateCount && !accepted; i++)
	{
		for (int j = 0; j < outputCandidateCount && !accepted; j++)
		{
			SpeakerArrangement inputArrangement = inputCandidates[i];
			SpeakerArrangement outputArrangement = outputCandidates[j];
			const tresult result = vst3Processor->setBusArrangements(
				vst3InputBusCount > 0 ? &inputArrangement : NULL, vst3InputBusCount > 0 ? 1 : 0,
				vst3OutputBusCount > 0 ? &outputArrangement : NULL, vst3OutputBusCount > 0 ? 1 : 0);
			accepted = result == kResultTrue
				&& (vst3InputBusCount == 0 || vst3BusChannelCount(kInput) == inputChannelCount)
				&& (vst3OutputBusCount == 0 || vst3BusChannelCount(kOutput) == outputChannelCount);
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
			vst3Processor->setBusArrangements(
				vst3InputBusCount > 0 && inputArrangement != SpeakerArr::kEmpty ? &inputArrangement : NULL,
				vst3InputBusCount > 0 && inputArrangement != SpeakerArr::kEmpty ? 1 : 0,
				vst3OutputBusCount > 0 && outputArrangement != SpeakerArr::kEmpty ? &outputArrangement : NULL,
				vst3OutputBusCount > 0 && outputArrangement != SpeakerArr::kEmpty ? 1 : 0);
		}
	}

	if (vst3InputBusCount > 0)
		vst3InputChannelCount = vst3BusChannelCount(kInput);
	if (vst3OutputBusCount > 0)
		vst3OutputChannelCount = vst3BusChannelCount(kOutput);
}

void VSTPluginInstance::applyVST3BusActivation()
{
	for (int i = 0; i < vst3InputBusCount; i++)
		vst3Component->activateBus(kAudio, kInput, i, i == 0);
	for (int i = 0; i < vst3OutputBusCount; i++)
		vst3Component->activateBus(kAudio, kOutput, i, i == 0);
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

int VSTPluginInstance::speakerArrangementCandidatesForChannelCount(int count, SpeakerArrangement* candidates) const
{
	// First candidate matches the Windows channel-mask ordering for that
	// width; a second candidate covers plugins that only announce the other
	// common arrangement of the same width.
	switch (count)
	{
	case 1:
		candidates[0] = SpeakerArr::kMono;
		return 1;
	case 2:
		candidates[0] = SpeakerArr::kStereo;
		return 1;
	case 4:
		candidates[0] = SpeakerArr::k40Music;
		candidates[1] = SpeakerArr::k40Cine;
		return 2;
	case 5:
		candidates[0] = SpeakerArr::k50;
		return 1;
	case 6:
		candidates[0] = SpeakerArr::k51;
		return 1;
	case 7:
		candidates[0] = SpeakerArr::k61Cine;
		return 1;
	case 8:
		candidates[0] = SpeakerArr::k71Music;
		candidates[1] = SpeakerArr::k71Cine;
		return 2;
	case 10:
		candidates[0] = SpeakerArr::k71_2;
		return 1;
	case 12:
		candidates[0] = SpeakerArr::k71_4;
		return 1;
	default:
		return 0;
	}
}
