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

// VSTPluginInstance is split across several translation units to keep each one
// focused:
//   - VSTPluginInstance.cpp        : construction/teardown, the VST2/VST3 dispatch
//                                    accessors, prepareForProcessing, the audio
//                                    process group, and the host callback plumbing.
//   - VSTPluginInstance.VST2.cpp   : VST2 hosting (the AEffect host callback and
//                                    initializeVST2 with its __try/__except guard).
//   - VSTPluginInstance.VST3.cpp   : VST3 hosting (initialize/release, bus setup).
//   - VSTPluginInstance.Editor.cpp : plugin editor window management.
//   - VSTPluginInstance.State.cpp  : chunk/parameter state read/write.
//   - VSTPluginInstanceInternal.h  : the nested helper classes VST3MemoryStream
//                                    and VST3HostContext, shared across those units.

#include "stdafx.h"
#include <wincrypt.h>
#include <inttypes.h>
#include "StringHelper.h"
#include "LogHelper.h"
#include "../Version.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "VSTPluginInstanceInternal.h"
#include "pluginterfaces/base/futils.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

class EmptyVST3ParameterChanges : public IParameterChanges
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IParameterChanges)
		QUERY_INTERFACE(iid, obj, IParameterChanges::iid, IParameterChanges)
		*obj = NULL;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() override { return 2; }
	uint32 PLUGIN_API release() override { return 1; }
	int32 PLUGIN_API getParameterCount() override { return 0; }
	IParamValueQueue* PLUGIN_API getParameterData(int32) override { return NULL; }
	IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32& index) override
	{
		index = -1;
		return NULL;
	}
};

class EmptyVST3EventList : public IEventList
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IEventList)
		QUERY_INTERFACE(iid, obj, IEventList::iid, IEventList)
		*obj = NULL;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() override { return 2; }
	uint32 PLUGIN_API release() override { return 1; }
	int32 PLUGIN_API getEventCount() override { return 0; }
	tresult PLUGIN_API getEvent(int32, Event&) override { return kInvalidArgument; }
	tresult PLUGIN_API addEvent(Event&) override { return kResultFalse; }
};

static EmptyVST3ParameterChanges emptyVST3ParameterChanges;
static EmptyVST3EventList emptyVST3EventList;

void VST2EffectDeleter::operator()(vst_effect_t* effect) const noexcept
{
	if (effect != NULL)
	{
		__try
		{
			effect->control(effect, VST_EFFECT_OPCODE_DESTROY, 0, 0, NULL, 0.0f);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// A broken plugin must not take down the host while its RAII owner
			// unwinds. The plugin's memory is no longer safely reclaimable here.
		}
	}
}

VSTPluginInstance::VSTPluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
	: library(library), processLevel(processLevel)
{
}

VSTPluginInstance::~VSTPluginInstance()
{
	automateFunc = nullptr;
	sizeWindowFunc = nullptr;
	releaseVST3();
	effect.reset();
}

bool VSTPluginInstance::initialize()
{
	if (library->isVST3())
		return initializeVST3();

	// initializeVST2 must stay free of objects requiring stack unwinding because it
	// uses a __try/__except guard (MSVC C2712), so log its failure reasons here where
	// constructing the std::wstring temporary from getLibPath is allowed.
	bool result = initializeVST2();
	if (!result)
		LogF(L"Loading VST2 plugin %s failed due to an exception.", library->getLibPath().c_str());
	else if (effect == NULL)
		LogF(L"VST2 plugin %s has wrong magic number, not loading it.", library->getLibPath().c_str());
	return result;
}

int VSTPluginInstance::numInputs() const
{
	if (library->isVST3())
		return vst3InputChannelCount;
	if (effect == NULL)
		return 0;

	return effect->num_inputs;
}

int VSTPluginInstance::numOutputs() const
{
	if (library->isVST3())
		return vst3OutputChannelCount;
	if (effect == NULL)
		return 0;

	return effect->num_outputs;
}

bool VSTPluginInstance::canReplacing() const
{
	if (library->isVST3())
		return true;
	if (effect == NULL)
		return true;

	return (effect->flags & VST_EFFECT_FLAG_SUPPORTS_FLOAT) != 0;
}

int VSTPluginInstance::uniqueID() const
{
	if (library->isVST3())
	{
		const PClassInfo& classInfo = library->getVST3ClassInfo();
		int result = 0;
		memcpy(&result, classInfo.cid, sizeof(result));
		return result;
	}
	if (effect == NULL)
		return 0;

	return effect->unique_id;
}

std::wstring VSTPluginInstance::getName() const
{
	if (library->isVST3())
		return StringHelper::toWString(library->getVST3ClassInfo().name, CP_UTF8);
	if (effect == NULL)
		return L"";

	char buf[256];
	memset(buf, 0, sizeof(buf));
	effect->control(effect.get(), VST_EFFECT_OPCODE_EFFECT_NAME, 0, 0, buf, 0.0f);
	buf[255] = '\0'; // just to be sure

	return StringHelper::toWString(buf, CP_UTF8);
}

int VSTPluginInstance::getUsedChannelCount() const
{
	return usedChannelCount;
}

void VSTPluginInstance::setUsedChannelCount(int count)
{
	usedChannelCount = count;
}

float VSTPluginInstance::getSampleRate() const
{
	return sampleRate;
}

int VSTPluginInstance::getProcessLevel() const
{
	return processLevel;
}

bool VSTPluginInstance::canDoubleReplacing() const
{
	if (library->isVST3())
		return vst3SupportsDouble;
	return (effect->flags & VST_EFFECT_FLAG_SUPPORTS_DOUBLE) != 0;
}

int VSTPluginInstance::getInitialDelay() const
{
	if (library->isVST3())
		return vst3Processor != NULL ? (int)vst3Processor->getLatencySamples() : 0;
	if (effect == NULL)
		return 0;

	return effect->delay;
}

void VSTPluginInstance::setProcessLevel(int value)
{
	processLevel = value;
}

int VSTPluginInstance::getLanguage() const
{
	return language;
}

void VSTPluginInstance::setLanguage(int value)
{
	language = value;
}

void VSTPluginInstance::prepareForProcessing(float sampleRate, int blockSize)
{
	if (library->isVST3())
	{
		if (vst3Processor == NULL || vst3Component == NULL)
			return;

		this->sampleRate = sampleRate;
		configureVST3Buses(usedChannelCount > 0 ? usedChannelCount : max(vst3InputChannelCount, vst3OutputChannelCount));
		ProcessSetup setup;
		setup.processMode = kRealtime;
		setup.symbolicSampleSize = vst3SupportsDouble ? kSample64 : kSample32;
		setup.maxSamplesPerBlock = blockSize;
		setup.sampleRate = sampleRate;
		vst3Processor->setupProcessing(setup);
		vst3SamplePosition = 0;
		return;
	}

	if (effect == NULL)
		return;

	this->sampleRate = sampleRate;
	effect->control(effect.get(), VST_EFFECT_OPCODE_SET_SAMPLE_RATE, 0, 0, NULL, sampleRate);
	effect->control(effect.get(), VST_EFFECT_OPCODE_SET_BLOCK_SIZE, 0, blockSize, NULL, 0.0f);
}

void VSTPluginInstance::startProcessing()
{
	if (library->isVST3())
	{
		if (vst3Component != NULL && !vst3Active)
		{
			vst3Component->setActive(true);
			vst3Active = true;
		}
		if (vst3Processor != NULL && !vst3Processing)
		{
			vst3Processor->setProcessing(true);
			vst3Processing = true;
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect.get(), VST_EFFECT_OPCODE_SUSPEND, 0, 1, NULL, 0.0f);
	effect->control(effect.get(), VST_EFFECT_OPCODE_PROCESS_BEGIN, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::processReplacing(float** inputArray, float** outputArray, int frameCount)
{
	if (library->isVST3())
	{
		if (vst3Processor == NULL)
			return;
		AudioBusBuffers inputBuffers;
		inputBuffers.numChannels = numInputs();
		inputBuffers.channelBuffers32 = inputArray;
		AudioBusBuffers outputBuffers;
		outputBuffers.numChannels = numOutputs();
		outputBuffers.channelBuffers32 = outputArray;
		ProcessData data;
		data.processMode = kRealtime;
		data.symbolicSampleSize = kSample32;
		data.numSamples = frameCount;
		data.numInputs = vst3InputBusCount > 0 ? 1 : 0;
		data.numOutputs = vst3OutputBusCount > 0 ? 1 : 0;
		data.inputs = data.numInputs > 0 ? &inputBuffers : NULL;
		data.outputs = data.numOutputs > 0 ? &outputBuffers : NULL;
		data.inputParameterChanges = &emptyVST3ParameterChanges;
		data.inputEvents = &emptyVST3EventList;
		data.processContext = &vst3ProcessContext;
		vst3ProcessContext.state = ProcessContext::kPlaying | ProcessContext::kContTimeValid;
		vst3ProcessContext.sampleRate = sampleRate;
		vst3ProcessContext.projectTimeSamples = vst3SamplePosition;
		vst3ProcessContext.continousTimeSamples = vst3SamplePosition;
		if (vst3Processor->process(data) == kResultOk)
			vst3SamplePosition += frameCount;
		return;
	}

	if (effect == NULL)
		return;

	effect->process_float(effect.get(), inputArray, outputArray, frameCount);
}

void VSTPluginInstance::processDoubleReplacing(double** inputArray, double** outputArray, int frameCount)
{
	if (library->isVST3())
	{
		if (vst3Processor == NULL)
			return;
		AudioBusBuffers inputBuffers;
		inputBuffers.numChannels = numInputs();
		inputBuffers.channelBuffers64 = inputArray;
		AudioBusBuffers outputBuffers;
		outputBuffers.numChannels = numOutputs();
		outputBuffers.channelBuffers64 = outputArray;
		ProcessData data;
		data.processMode = kRealtime;
		data.symbolicSampleSize = kSample64;
		data.numSamples = frameCount;
		data.numInputs = vst3InputBusCount > 0 ? 1 : 0;
		data.numOutputs = vst3OutputBusCount > 0 ? 1 : 0;
		data.inputs = data.numInputs > 0 ? &inputBuffers : NULL;
		data.outputs = data.numOutputs > 0 ? &outputBuffers : NULL;
		data.inputParameterChanges = &emptyVST3ParameterChanges;
		data.inputEvents = &emptyVST3EventList;
		data.processContext = &vst3ProcessContext;
		vst3ProcessContext.state = ProcessContext::kPlaying | ProcessContext::kContTimeValid;
		vst3ProcessContext.sampleRate = sampleRate;
		vst3ProcessContext.projectTimeSamples = vst3SamplePosition;
		vst3ProcessContext.continousTimeSamples = vst3SamplePosition;
		if (vst3Processor->process(data) == kResultOk)
			vst3SamplePosition += frameCount;
		return;
	}

	if (effect == NULL)
		return;

	effect->process_double(effect.get(), inputArray, outputArray, frameCount);
}

void VSTPluginInstance::process(float** inputArray, float** outputArray, int frameCount)
{
	if (library->isVST3())
	{
		processReplacing(inputArray, outputArray, frameCount);
		return;
	}

	if (effect == NULL)
		return;

	effect->process(effect.get(), inputArray, outputArray, frameCount);
}

void VSTPluginInstance::stopProcessing()
{
	if (library->isVST3())
	{
		if (vst3Processor != NULL && vst3Processing)
		{
			vst3Processor->setProcessing(false);
			vst3Processing = false;
		}
		if (vst3Component != NULL && vst3Active)
		{
			vst3Component->setActive(false);
			vst3Active = false;
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect.get(), VST_EFFECT_OPCODE_PROCESS_END, 0, 0, NULL, 0.0f);
	effect->control(effect.get(), VST_EFFECT_OPCODE_SUSPEND, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::stopProcessingSafely() noexcept
{
	__try
	{
		stopProcessing();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// Cleanup is best effort across a third-party native-code boundary.
	}
}

void VSTPluginInstance::setAutomateFunc(std::function<void()> func)
{
	automateFunc = func;
}

void VSTPluginInstance::onAutomate()
{
	if (automateFunc)
		automateFunc();
}

void VSTPluginInstance::setSizeWindowFunc(std::function<void(int, int)> func)
{
	sizeWindowFunc = func;
}

void VSTPluginInstance::onSizeWindow(int w, int h)
{
	if (vst3EditorHostWindow)
	{
		// w/h arrive in physical pixels from the plugin's resizeView request.
		// The native host window takes physical px; the Qt frame
		// (sizeWindowFunc) takes logical px, so divide by the editor scale.
		SetWindowPos(vst3EditorHostWindow.get(), NULL, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		if (sizeWindowFunc)
		{
			double s = editorScaleFactor > 0.0 ? editorScaleFactor : 1.0;
			sizeWindowFunc(max(1, (int)(w / s + 0.5)), max(1, (int)(h / s + 0.5)));
		}
		return;
	}
	if (sizeWindowFunc)
		sizeWindowFunc(w, h);
}
