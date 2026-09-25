/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"
#include "VST3Lifecycle.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
	class TransitionGuard
	{
	public:
		explicit TransitionGuard(atomic<bool>& flag) : flag(flag)
		{
			bool expected = false;
			acquired = flag.compare_exchange_strong(expected, true, memory_order_acq_rel);
		}

		~TransitionGuard()
		{
			if (acquired)
				flag.store(false, memory_order_release);
		}

		explicit operator bool() const noexcept { return acquired; }

	private:
		atomic<bool>& flag;
		bool acquired = false;
	};
}

void VST3Lifecycle::setInterfaces(IComponent* newComponent, IAudioProcessor* newProcessor)
{
	lock_guard<std::mutex> lock(mutex);
	component = newComponent;
	processor = newProcessor;
	prepared = false;
	active = false;
	processing.store(false, memory_order_release);
	editorSession = false;
	parameterFlushInProgress.store(false, memory_order_release);
}

void VST3Lifecycle::clearInterfaces()
{
	setInterfaces(nullptr, nullptr);
}

bool VST3Lifecycle::setupProcessing(const ProcessSetup& setup)
{
	TransitionGuard transition(parameterFlushInProgress);
	if (!transition)
		return false;

	lock_guard<std::mutex> lock(mutex);
	if (processor == nullptr || active || editorSession || processing.load(memory_order_acquire))
		return false;
	ProcessSetup requested = setup;
	if (processor->setupProcessing(requested) != kResultOk)
		return false;
	prepared = true;
	return true;
}

bool VST3Lifecycle::prepareOnce(int32 symbolicSampleSize)
{
	TransitionGuard transition(parameterFlushInProgress);
	if (!transition)
		return false;

	lock_guard<std::mutex> lock(mutex);
	if (active || editorSession || processing.load(memory_order_acquire))
		return false;
	return prepareOnceLocked(symbolicSampleSize);
}

bool VST3Lifecycle::prepareOnceLocked(int32 symbolicSampleSize)
{
	if (prepared)
		return true;
	if (processor == nullptr)
		return false;

	ProcessSetup setup;
	setup.processMode = kRealtime;
	setup.symbolicSampleSize = symbolicSampleSize;
	setup.maxSamplesPerBlock = 1;
	setup.sampleRate = 48000.0;
	if (processor->setupProcessing(setup) != kResultOk)
		return false;
	prepared = true;
	return true;
}

bool VST3Lifecycle::activate()
{
	TransitionGuard transition(parameterFlushInProgress);
	if (!transition)
		return false;

	lock_guard<std::mutex> lock(mutex);
	if (editorSession || processing.load(memory_order_acquire))
		return false;
	return activateLocked();
}

bool VST3Lifecycle::activateLocked()
{
	if (active)
		return true;
	if (component == nullptr || component->setActive(true) != kResultOk)
		return false;
	active = true;
	return true;
}

void VST3Lifecycle::deactivate()
{
	TransitionGuard transition(parameterFlushInProgress);
	if (!transition)
		return;

	lock_guard<std::mutex> lock(mutex);
	if (editorSession || processing.load(memory_order_acquire))
		return;
	deactivateLocked();
}

void VST3Lifecycle::deactivateLocked()
{
	if (component != nullptr && active)
		component->setActive(false);
	active = false;
}

bool VST3Lifecycle::startProcessing()
{
	lock_guard<std::mutex> lock(mutex);
	if (component == nullptr || processor == nullptr || processing.load(memory_order_acquire))
		return false;

	// Publish the transition before calling the plug-in. A synchronous
	// performEdit must leave its change queued instead of starting an idle flush.
	processing.store(true, memory_order_release);
	if (!activateLocked() || processor->setProcessing(true) != kResultOk)
	{
		deactivateLocked();
		processing.store(false, memory_order_release);
		return false;
	}
	return true;
}

void VST3Lifecycle::stopProcessing()
{
	lock_guard<std::mutex> lock(mutex);
	const bool wasProcessing = processing.load(memory_order_acquire);
	// Keep callbacks in queue-only mode through both plug-in calls.
	processing.store(true, memory_order_release);
	if (processor != nullptr && wasProcessing)
		processor->setProcessing(false);
	deactivateLocked();
	processing.store(false, memory_order_release);
}

bool VST3Lifecycle::beginEditorSession(int32 symbolicSampleSize)
{
	TransitionGuard transition(parameterFlushInProgress);
	if (!transition)
		return false;

	lock_guard<std::mutex> lock(mutex);
	if (component == nullptr || processor == nullptr || editorSession || active
		|| processing.load(memory_order_acquire))
	{
		return false;
	}
	if (!prepareOnceLocked(symbolicSampleSize) || !activateLocked())
		return false;
	if (processor->setProcessing(true) != kResultOk)
	{
		deactivateLocked();
		return false;
	}
	editorSession = true;
	return true;
}

void VST3Lifecycle::endEditorSession()
{
	TransitionGuard transition(parameterFlushInProgress);
	if (!transition)
		return;

	lock_guard<std::mutex> lock(mutex);
	if (!editorSession)
		return;
	editorSession = false;
	if (processor != nullptr)
		processor->setProcessing(false);
	deactivateLocked();
}

bool VST3Lifecycle::flushParameters(ProcessData& data,
	const HasPendingChanges& hasPendingChanges,
	const PrepareParameterChanges& prepareParameterChanges,
	int32 symbolicSampleSize)
{
	if (component == nullptr || processor == nullptr
		|| processing.load(memory_order_acquire) || !hasPendingChanges())
	{
		return false;
	}

	TransitionGuard transition(parameterFlushInProgress);
	if (!transition)
		return false;

	lock_guard<std::mutex> lock(mutex);
	if (processing.load(memory_order_acquire) || !hasPendingChanges())
		return false;

	// An editor session already holds the component in the Processing state.
	// Other idle instances use one activation cycle around the zero-sample call.
	const bool sessionFlush = editorSession;
	bool activatedForFlush = false;
	if (!sessionFlush)
	{
		if (!active)
		{
			if (!prepareOnceLocked(symbolicSampleSize) || !activateLocked())
				return false;
			activatedForFlush = true;
		}
		if (processor->setProcessing(true) != kResultOk)
		{
			if (activatedForFlush)
				deactivateLocked();
			return false;
		}
	}

	data.inputParameterChanges = prepareParameterChanges();
	processor->process(data);

	if (!sessionFlush)
	{
		processor->setProcessing(false);
		if (activatedForFlush)
			deactivateLocked();
	}
	return true;
}

bool VST3Lifecycle::audioProcessing() const noexcept
{
	return processing.load(memory_order_acquire);
}

bool VST3Lifecycle::canProcessNow() const
{
	lock_guard<std::mutex> lock(mutex);
	return active && (editorSession || processing.load(memory_order_acquire));
}
