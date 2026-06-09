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

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

bool VSTPluginInstance::initializeVST3()
{
	vst3HostContext = new VST3HostContext(this);

	const PClassInfo& classInfo = library->getVST3ClassInfo();
	FUID componentId(classInfo.cid);
	TUID componentIid;
	IComponent::iid.toTUID(componentIid);
	if (library->getFactory()->createInstance(componentId, componentIid, (void**)&vst3Component) != kResultOk || vst3Component == NULL)
	{
		LogF(L"Could not create IComponent instance of VST3 plugin %s.", library->getLibPath().c_str());
		return false;
	}

	vst3Component->setIoMode(kSimple);
	if (vst3Component->initialize(static_cast<IHostApplication*>(vst3HostContext)) != kResultOk)
	{
		LogF(L"Could not initialize IComponent of VST3 plugin %s.", library->getLibPath().c_str());
		return false;
	}

	TUID processorIid;
	IAudioProcessor::iid.toTUID(processorIid);
	if (vst3Component->queryInterface(processorIid, (void**)&vst3Processor) != kResultOk || vst3Processor == NULL)
	{
		LogF(L"VST3 plugin %s does not provide the IAudioProcessor interface.", library->getLibPath().c_str());
		return false;
	}

	TUID controllerClassId;
	memset(controllerClassId, 0, sizeof(controllerClassId));
	if (vst3Component->getControllerClassId(controllerClassId) == kResultOk)
	{
		TUID controllerIid;
		IEditController::iid.toTUID(controllerIid);
		library->getFactory()->createInstance(controllerClassId, controllerIid, (void**)&vst3Controller);
	}
	if (vst3Controller == NULL)
	{
		TUID controllerIid;
		IEditController::iid.toTUID(controllerIid);
		vst3Component->queryInterface(controllerIid, (void**)&vst3Controller);
	}
	if (vst3Controller != NULL)
	{
		vst3Controller->initialize(static_cast<IHostApplication*>(vst3HostContext));
		vst3Controller->setComponentHandler(vst3HostContext);

		VST3MemoryStream* stream = new VST3MemoryStream();
		if (vst3Component->getState(stream) == kResultOk)
		{
			stream->seek(0, IBStream::kIBSeekSet);
			vst3Controller->setComponentState(stream);
		}
		stream->release();

		TUID connectionPointIid;
		Steinberg::Vst::IConnectionPoint::iid.toTUID(connectionPointIid);
		if (vst3Component->queryInterface(connectionPointIid, (void**)&vst3ComponentConnection) == kResultOk
			&& vst3Controller->queryInterface(connectionPointIid, (void**)&vst3ControllerConnection) == kResultOk)
		{
			vst3ComponentConnection->connect(vst3ControllerConnection);
			vst3ControllerConnection->connect(vst3ComponentConnection);
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
	stopProcessing();

	if (vst3Controller != NULL)
		vst3Controller->setComponentHandler(NULL);

	if (vst3ComponentConnection != NULL && vst3ControllerConnection != NULL)
	{
		vst3ComponentConnection->disconnect(vst3ControllerConnection);
		vst3ControllerConnection->disconnect(vst3ComponentConnection);
	}
	if (vst3ComponentConnection != NULL)
	{
		vst3ComponentConnection->release();
		vst3ComponentConnection = NULL;
	}
	if (vst3ControllerConnection != NULL)
	{
		vst3ControllerConnection->release();
		vst3ControllerConnection = NULL;
	}
	if (vst3Controller != NULL)
	{
		vst3Controller->terminate();
		vst3Controller->release();
		vst3Controller = NULL;
	}
	if (vst3Processor != NULL)
	{
		vst3Processor->release();
		vst3Processor = NULL;
	}
	if (vst3Component != NULL)
	{
		vst3Component->terminate();
		vst3Component->release();
		vst3Component = NULL;
	}
	if (vst3HostContext != NULL)
	{
		vst3HostContext->release();
		vst3HostContext = NULL;
	}
}

void VSTPluginInstance::configureVST3Buses(int requestedChannelCount)
{
	if (vst3Component == NULL || vst3Processor == NULL)
		return;

	int channelCount = max(1, requestedChannelCount);
	SpeakerArrangement inputArrangement = speakerArrangementForChannelCount(channelCount);
	SpeakerArrangement outputArrangement = speakerArrangementForChannelCount(channelCount);

	if (inputArrangement == SpeakerArr::kEmpty || outputArrangement == SpeakerArr::kEmpty)
	{
		BusInfo busInfo;
		memset(&busInfo, 0, sizeof(busInfo));
		if (vst3InputBusCount > 0 && vst3Component->getBusInfo(kAudio, kInput, 0, busInfo) == kResultOk && busInfo.channelCount > 0)
			inputArrangement = speakerArrangementForChannelCount(busInfo.channelCount);
		if (vst3OutputBusCount > 0 && vst3Component->getBusInfo(kAudio, kOutput, 0, busInfo) == kResultOk && busInfo.channelCount > 0)
			outputArrangement = speakerArrangementForChannelCount(busInfo.channelCount);
	}

	for (int i = 0; i < vst3InputBusCount; i++)
		vst3Component->activateBus(kAudio, kInput, i, i == 0);
	for (int i = 0; i < vst3OutputBusCount; i++)
		vst3Component->activateBus(kAudio, kOutput, i, i == 0);

	if (inputArrangement != SpeakerArr::kEmpty || outputArrangement != SpeakerArr::kEmpty)
	{
		vst3Processor->setBusArrangements(
			vst3InputBusCount > 0 && inputArrangement != SpeakerArr::kEmpty ? &inputArrangement : NULL,
			vst3InputBusCount > 0 && inputArrangement != SpeakerArr::kEmpty ? 1 : 0,
			vst3OutputBusCount > 0 && outputArrangement != SpeakerArr::kEmpty ? &outputArrangement : NULL,
			vst3OutputBusCount > 0 && outputArrangement != SpeakerArr::kEmpty ? 1 : 0);
	}

	BusInfo busInfo;
	memset(&busInfo, 0, sizeof(busInfo));
	if (vst3InputBusCount > 0 && vst3Component->getBusInfo(kAudio, kInput, 0, busInfo) == kResultOk)
		vst3InputChannelCount = max(0, busInfo.channelCount);
	if (vst3OutputBusCount > 0 && vst3Component->getBusInfo(kAudio, kOutput, 0, busInfo) == kResultOk)
		vst3OutputChannelCount = max(0, busInfo.channelCount);
}

SpeakerArrangement VSTPluginInstance::speakerArrangementForChannelCount(int count) const
{
	switch (count)
	{
	case 1:
		return SpeakerArr::kMono;
	case 2:
		return SpeakerArr::kStereo;
	case 4:
		return SpeakerArr::k40Music;
	case 5:
		return SpeakerArr::k50;
	case 6:
		return SpeakerArr::k51;
	case 7:
		return SpeakerArr::k61Cine;
	case 8:
		return SpeakerArr::k71Music;
	default:
		return SpeakerArr::kEmpty;
	}
}
