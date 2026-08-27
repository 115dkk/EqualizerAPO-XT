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
#include "text/WideString.h"
#include "platform/windows/TextEncoding.h"
#include <wincrypt.h>
#include <inttypes.h>
#include "services/logging/Logging.h"
#include "../Version.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "VSTPluginInstanceInternal.h"
#include "pluginterfaces/base/futils.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
// Clears the parameter-flush guard on scope exit. Was defined inline three
// times at its use sites (audit #275 TD-25/C4 stage 1).
struct FlushFlagReset
{
	std::atomic<bool>& flag;
	~FlushFlagReset() { flag.store(false, std::memory_order_release); }
};
}

// The parameter-change list handed to IAudioProcessor::process. Fixed-size
// and allocation-free after construction, so filling it on the audio thread
// stays RT-safe. Each parameter gets a single-point queue (sample offset 0):
// this host forwards GUI edits, not sample-accurate automation curves.
// Ported from the ripDZL fork's VST3 compatibility work
// (github.com/ripDZL/EqualizerAPO-XT/pull/1).
class VSTPluginInstance::VST3ParameterChanges : public IParameterChanges
{
public:
	class ParamValueQueue : public IParamValueQueue
	{
	public:
		tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
		{
			QUERY_INTERFACE(iid, obj, FUnknown::iid, IParamValueQueue)
			QUERY_INTERFACE(iid, obj, IParamValueQueue::iid, IParamValueQueue)
			*obj = NULL;
			return kNoInterface;
		}

		// Embedded in the owning list; lifetime is the instance's.
		uint32 PLUGIN_API addRef() override { return 2; }
		uint32 PLUGIN_API release() override { return 1; }
		ParamID PLUGIN_API getParameterId() override { return id; }
		int32 PLUGIN_API getPointCount() override { return hasPoint ? 1 : 0; }
		tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& pointValue) override
		{
			if (!hasPoint || index != 0)
				return kInvalidArgument;
			sampleOffset = 0;
			pointValue = value;
			return kResultOk;
		}
		tresult PLUGIN_API addPoint(int32, ParamValue pointValue, int32& index) override
		{
			// Later points collapse onto the single slot: the last value of a
			// block wins, which is the semantic a control edit needs.
			value = pointValue;
			hasPoint = true;
			index = 0;
			return kResultOk;
		}

		void set(ParamID parameterId, ParamValue pointValue)
		{
			id = parameterId;
			value = pointValue;
			hasPoint = true;
		}

	private:
		ParamID id = 0;
		ParamValue value = 0.0;
		bool hasPoint = false;
	};

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IParameterChanges)
		QUERY_INTERFACE(iid, obj, IParameterChanges::iid, IParameterChanges)
		*obj = NULL;
		return kNoInterface;
	}

	// Owned by the instance through a unique_ptr; refcounting is vestigial.
	uint32 PLUGIN_API addRef() override { return 2; }
	uint32 PLUGIN_API release() override { return 1; }
	int32 PLUGIN_API getParameterCount() override { return count; }
	IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
	{
		return index >= 0 && index < count ? &queues[index] : NULL;
	}
	IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override
	{
		for (int32 i = 0; i < count; i++)
		{
			if (queues[i].getParameterId() == id)
			{
				index = i;
				return &queues[i];
			}
		}
		if (count >= (int32)queues.size())
		{
			index = -1;
			return NULL;
		}
		index = count++;
		queues[index].set(id, 0.0);
		return &queues[index];
	}

	void clear() { count = 0; }
	void add(ParamID id, ParamValue value)
	{
		int32 index;
		IParamValueQueue* queue = addParameterData(id, index);
		if (queue != NULL)
			queue->addPoint(0, value, index);
	}

private:
	array<ParamValueQueue, VSTPluginInstance::vst3ParameterEditQueueSize> queues{};
	int32 count = 0;
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
	if (library->isVST3())
		vst3InputParameterChanges.reset(new VST3ParameterChanges());
}

VSTPluginInstance::~VSTPluginInstance()
{
	automateFunc = nullptr;
	sizeWindowFunc = nullptr;
	releaseVST3();
	effect.reset();
}

void VSTPluginInstance::onVST3ParameterEdit(ParamID id, ParamValue value)
{
	queueVST3ParameterEdit(id, value);
	const bool processing = vst3Processing.load(memory_order_acquire);
	flushVST3ParameterChanges();
	// While audio is running, the component does not own this value until its
	// next process call drains inputParameterChanges; saving synchronously
	// here would persist the previous component state. Editor instances are
	// stopped and take the immediate path below.
	if (!processing && !vst3Processing.load(memory_order_acquire)
		&& vst3ParameterEditRead.load(memory_order_acquire) == vst3ParameterEditWrite.load(memory_order_acquire))
		onAutomate();
}

void VSTPluginInstance::queueVST3ParameterEdit(ParamID id, ParamValue value)
{
	// Single-producer ring: every writer runs on the instance's control
	// thread (the Editor's GUI thread for performEdit and writeToEffect; in
	// the engine, the configuration loader before processing starts). The
	// consumer is whoever runs the next process call. A full ring drops the
	// edit - the following edit of the same control supersedes it anyway.
	unsigned writeIndex = vst3ParameterEditWrite.load(memory_order_relaxed);
	unsigned nextWriteIndex = (writeIndex + 1) % vst3ParameterEditQueueSize;
	if (nextWriteIndex != vst3ParameterEditRead.load(memory_order_acquire))
	{
		vst3ParameterEditQueue[writeIndex] = { id, value };
		vst3ParameterEditWrite.store(nextWriteIndex, memory_order_release);
	}
}

IParameterChanges* VSTPluginInstance::prepareVST3ParameterChanges()
{
	if (vst3InputParameterChanges == NULL)
		return NULL;

	// Drain the pending edits into the fixed-size change list. Runs on the
	// audio thread between blocks (or on the control thread during an idle
	// flush); allocation-free either way.
	vst3InputParameterChanges->clear();
	unsigned readIndex = vst3ParameterEditRead.load(memory_order_relaxed);
	unsigned writeIndex = vst3ParameterEditWrite.load(memory_order_acquire);
	while (readIndex != writeIndex)
	{
		const PendingVST3ParameterEdit& edit = vst3ParameterEditQueue[readIndex];
		vst3InputParameterChanges->add(edit.id, edit.value);
		readIndex = (readIndex + 1) % vst3ParameterEditQueueSize;
	}
	vst3ParameterEditRead.store(readIndex, memory_order_release);
	return vst3InputParameterChanges.get();
}

void VSTPluginInstance::flushVST3ParameterChanges()
{
	// Deliver queued edits to a plug-in that is not currently processing:
	// per the VST3 contract the processor only consumes IParameterChanges
	// inside process(), so an idle instance gets a zero-sample call. The
	// running case needs nothing - the next audio block drains the queue.
	if (vst3Component == NULL || vst3Processor == NULL
		|| vst3Processing.load(memory_order_acquire)
		|| vst3ParameterEditRead.load(memory_order_acquire) == vst3ParameterEditWrite.load(memory_order_acquire))
		return;
	bool expected = false;
	if (!vst3ParameterFlushInProgress.compare_exchange_strong(expected, true, memory_order_acq_rel))
		return;
	FlushFlagReset reset{ vst3ParameterFlushInProgress };

	lock_guard<mutex> lifecycleLock(vst3LifecycleMutex);
	if (vst3Processing.load(memory_order_acquire)
		|| vst3ParameterEditRead.load(memory_order_acquire) == vst3ParameterEditWrite.load(memory_order_acquire))
		return;

	// An open editor session already holds the plug-in in the Processing
	// state (beginVST3EditorSession), so the flush below is just the
	// buffer-less process call. Everything else takes the one-shot
	// activation path.
	const bool sessionFlush = vst3EditorSession;

	// An instance that never prepared for processing (an Editor preview
	// before the first analysis run) still needs a valid setup before it may
	// be activated.
	bool activatedForFlush = false;
	if (!sessionFlush)
	{
		if (!vst3Active)
		{
			if (sampleRate <= 0.0f)
			{
				ProcessSetup setup;
				setup.processMode = kRealtime;
				setup.symbolicSampleSize = vst3SupportsDouble ? kSample64 : kSample32;
				setup.maxSamplesPerBlock = 1;
				setup.sampleRate = 48000.0;
				if (vst3Processor->setupProcessing(setup) != kResultOk)
					return;
			}
			if (vst3Component->setActive(true) != kResultOk)
				return;
			vst3Active = true;
			activatedForFlush = true;
		}

		if (vst3Processor->setProcessing(true) != kResultOk)
		{
			if (activatedForFlush)
			{
				vst3Component->setActive(false);
				vst3Active = false;
			}
			return;
		}
	}

	ProcessData data;
	data.processMode = kRealtime;
	data.symbolicSampleSize = vst3SupportsDouble ? kSample64 : kSample32;
	data.numSamples = 0;
	data.numInputs = 0;
	data.numOutputs = 0;
	data.inputs = NULL;
	data.outputs = NULL;
	data.inputParameterChanges = prepareVST3ParameterChanges();
	data.inputEvents = &emptyVST3EventList;
	vst3Processor->process(data);

	if (!sessionFlush)
	{
		vst3Processor->setProcessing(false);

		if (activatedForFlush)
		{
			vst3Component->setActive(false);
			vst3Active = false;
		}
	}
}

void VSTPluginInstance::beginVST3EditorSession()
{
	// The docs place buffer-less parameter-flush process calls in the
	// Processing state (setActive(true) then setProcessing(true); FAQ: "the
	// host can call process without buffers ... in order to flush
	// parameters"). Enter that state once per editor session instead of
	// cycling it around every performEdit: a plug-in "has to reset its inner
	// processing state" on each setProcessing transition, and per-knob-tick
	// activation cycling is what made embedded editing unstable.
	if (vst3Component == NULL || vst3Processor == NULL)
		return;

	// Mirrors the flush guard: a plug-in may synchronously call performEdit
	// from setActive/setProcessing, and the nested flush attempt has to see
	// this flag and stay queued instead of deadlocking on the lifecycle
	// mutex.
	bool expected = false;
	if (!vst3ParameterFlushInProgress.compare_exchange_strong(expected, true, memory_order_acq_rel))
		return;
	FlushFlagReset reset{ vst3ParameterFlushInProgress };

	lock_guard<mutex> lifecycleLock(vst3LifecycleMutex);
	if (vst3EditorSession || vst3Active || vst3Processing.load(memory_order_acquire))
		return;

	if (sampleRate <= 0.0f)
	{
		// setupProcessing is only legal while deactivated; give a
		// never-prepared editor instance a valid setup first.
		ProcessSetup setup;
		setup.processMode = kRealtime;
		setup.symbolicSampleSize = vst3SupportsDouble ? kSample64 : kSample32;
		setup.maxSamplesPerBlock = 1;
		setup.sampleRate = 48000.0;
		if (vst3Processor->setupProcessing(setup) != kResultOk)
			return;
	}
	if (vst3Component->setActive(true) != kResultOk)
		return;
	vst3Active = true;
	if (vst3Processor->setProcessing(true) != kResultOk)
	{
		vst3Component->setActive(false);
		vst3Active = false;
		return;
	}
	vst3EditorSession = true;
}

void VSTPluginInstance::endVST3EditorSession()
{
	{
		bool expected = false;
		if (!vst3ParameterFlushInProgress.compare_exchange_strong(expected, true, memory_order_acq_rel))
			return;
		FlushFlagReset reset{ vst3ParameterFlushInProgress };

		lock_guard<mutex> lifecycleLock(vst3LifecycleMutex);
		if (!vst3EditorSession)
			return;
		vst3EditorSession = false;
		if (vst3Processor != NULL)
			vst3Processor->setProcessing(false);
		if (vst3Component != NULL)
		{
			vst3Component->setActive(false);
			vst3Active = false;
		}
	}
	// Edits a plug-in raised synchronously while leaving the session still
	// need to reach the processor; this one takes the one-shot path.
	flushVST3ParameterChanges();
}

bool VSTPluginInstance::vst3EditorSessionActive()
{
	lock_guard<mutex> lifecycleLock(vst3LifecycleMutex);
	return vst3EditorSession;
}

bool VSTPluginInstance::initialize()
{
	if (library->isVST3())
		return initializeVST3();

	// initializeVST2 must stay free of objects requiring stack unwinding because it
	// uses a __try/__except guard (MSVC C2712), so log its failure reasons here where
	// constructing the std::wstring temporary from getLibPath is allowed.
	// Audit #250 F040: the loader now reports which of its three failure
	// modes happened instead of blaming every one on "an exception".
	switch (initializeVST2())
	{
	case Vst2LoadResult::Loaded:
		return true;
	case Vst2LoadResult::Crashed:
		LogF(L"Loading VST2 plugin %s failed due to an exception.", library->getLibPath().c_str());
		return false;
	case Vst2LoadResult::NoEntryPoint:
		LogF(L"VST2 plugin %s returned no effect from its entry point, not loading it.", library->getLibPath().c_str());
		return false;
	case Vst2LoadResult::WrongMagicNumber:
	default:
		LogF(L"VST2 plugin %s has wrong magic number, not loading it.", library->getLibPath().c_str());
		return false;
	}
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

const std::vector<int>& VSTPluginInstance::getVST3InputChannelMapping() const
{
	return vst3InputChannelMapping;
}

const std::vector<int>& VSTPluginInstance::getVST3OutputChannelMapping() const
{
	return vst3OutputChannelMapping;
}

bool VSTPluginInstance::canReplacing() const
{
	if (library->isVST3())
		return true;
	if (effect == NULL)
		return true;

	return (effect->flags & VST_EFFECT_FLAG_SUPPORTS_FLOAT) != 0;
}

bool VSTPluginInstance::isVST3() const
{
	return library->isVST3();
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
		return wintext::toWideString(library->getVST3ClassInfo().name, CP_UTF8);
	if (effect == NULL)
		return L"";

	char buf[256];
	memset(buf, 0, sizeof(buf));
	effect->control(effect.get(), VST_EFFECT_OPCODE_EFFECT_NAME, 0, 0, buf, 0.0f);
	buf[255] = '\0'; // just to be sure

	return wintext::toWideString(buf, CP_UTF8);
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
	// Audit #250 F037: the sibling accessors all guard the unloaded case;
	// this one is currently unreachable without an effect, but it is one
	// refactoring away from being the exception.
	if (effect == NULL)
		return false;
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
		// The bus width was negotiated in initialize()/negotiateChannelCount()
		// and the host's buffer layout is already frozen to it. Renegotiating
		// here (e.g. to usedChannelCount, which can be a smaller remainder for
		// the last instance) could change the reported channel counts after
		// the buffers were sized, so only re-activate the buses.
		applyVST3BusActivation();
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
		lock_guard<mutex> lifecycleLock(vst3LifecycleMutex);
		if (vst3Component == NULL || vst3Processor == NULL || vst3Processing.load(memory_order_acquire))
			return;

		// Publish the transition before calling into the plug-in. A plug-in
		// may synchronously call the component handler from
		// setActive/setProcessing; those edits must stay queued instead of
		// attempting a nested idle flush.
		vst3Processing.store(true, memory_order_release);
		if (!vst3Active)
			vst3Active = vst3Component->setActive(true) == kResultOk;
		if (!vst3Active || vst3Processor->setProcessing(true) != kResultOk)
		{
			if (vst3Active)
			{
				vst3Component->setActive(false);
				vst3Active = false;
			}
			vst3Processing.store(false, memory_order_release);
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect.get(), VST_EFFECT_OPCODE_SUSPEND, 0, 1, NULL, 0.0f);
	effect->control(effect.get(), VST_EFFECT_OPCODE_PROCESS_BEGIN, 0, 0, NULL, 0.0f);
}

namespace
{
// The one VST3 process-call choreography behind both sample widths (audit
// #275 TD-25): the float and double bodies were 35-line near-clones whose
// only differences were the buffer field and the symbolic sample size.
template<typename SampleType>
struct Vst3SampleTraits;

template<>
struct Vst3SampleTraits<float>
{
	static constexpr int32 symbolicSampleSize = kSample32;
	static void attach(AudioBusBuffers& buffers, float** channels) { buffers.channelBuffers32 = channels; }
};

template<>
struct Vst3SampleTraits<double>
{
	static constexpr int32 symbolicSampleSize = kSample64;
	static void attach(AudioBusBuffers& buffers, double** channels) { buffers.channelBuffers64 = channels; }
};
}

template<typename SampleType>
void VSTPluginInstance::processVst3Replacing(SampleType** inputArray, SampleType** outputArray, int frameCount)
{
	if (vst3Processor == NULL)
		return;
	AudioBusBuffers inputBuffers;
	inputBuffers.numChannels = numInputs();
	Vst3SampleTraits<SampleType>::attach(inputBuffers, inputArray);
	AudioBusBuffers outputBuffers;
	outputBuffers.numChannels = numOutputs();
	Vst3SampleTraits<SampleType>::attach(outputBuffers, outputArray);
	ProcessData data;
	data.processMode = kRealtime;
	data.symbolicSampleSize = Vst3SampleTraits<SampleType>::symbolicSampleSize;
	data.numSamples = frameCount;
	data.numInputs = vst3InputBusCount > 0 ? 1 : 0;
	data.numOutputs = vst3OutputBusCount > 0 ? 1 : 0;
	data.inputs = data.numInputs > 0 ? &inputBuffers : NULL;
	data.outputs = data.numOutputs > 0 ? &outputBuffers : NULL;
	data.inputParameterChanges = prepareVST3ParameterChanges();
	data.inputEvents = &emptyVST3EventList;
	data.processContext = &vst3ProcessContext;
	vst3ProcessContext.state = ProcessContext::kPlaying | ProcessContext::kContTimeValid;
	vst3ProcessContext.sampleRate = sampleRate;
	vst3ProcessContext.projectTimeSamples = vst3SamplePosition;
	vst3ProcessContext.continousTimeSamples = vst3SamplePosition;
	if (vst3Processor->process(data) == kResultOk)
		vst3SamplePosition += frameCount;
}

void VSTPluginInstance::processReplacing(float** inputArray, float** outputArray, int frameCount)
{
	if (library->isVST3())
	{
		processVst3Replacing(inputArray, outputArray, frameCount);
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
		processVst3Replacing(inputArray, outputArray, frameCount);
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
		{
			lock_guard<mutex> lifecycleLock(vst3LifecycleMutex);
			const bool wasProcessing = vst3Processing.load(memory_order_acquire);
			// Keep component-handler callbacks in queue-only mode through
			// both plug-in calls below.
			vst3Processing.store(true, memory_order_release);
			if (vst3Processor != NULL && wasProcessing)
				vst3Processor->setProcessing(false);
			if (vst3Component != NULL && vst3Active)
			{
				vst3Component->setActive(false);
				vst3Active = false;
			}
			vst3Processing.store(false, memory_order_release);
		}
		// Edits that arrived during the audio run but after its last block
		// still need to reach the processor.
		flushVST3ParameterChanges();
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
