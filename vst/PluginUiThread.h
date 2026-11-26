/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>

class VSTPluginInstance;

// Plug-ins expect an instance to be initialised and destroyed on the thread
// that also shows their editors. Plug-ins built on JUCE go further: the thread
// that sets up a module's first live instance becomes its message thread, and
// an editor opened from any other thread waits for that one for ever.
// A process that shows editors names its UI thread by installing a Runner;
// VSTPluginInstance::initialize() and VSTPluginInstancePtr's deleter go through
// it. Without a Runner (the APO, the tools, the hosts) both run on the calling
// thread, as they always did.
namespace PluginUiThread
{
// call() returns after work ran on the UI thread; false means it was refused
// and will never run. post() queues work and returns at once. Both share one
// first-in, first-out order.
class Runner
{
public:
	virtual ~Runner() = default;
	virtual bool isUiThread() const = 0;
	virtual bool call(const std::function<void()>& work) = 0;
	virtual bool post(std::function<void()> work) = 0;
};

// Not owned. Removal waits for callers using the old runner to return. The
// owner stops accepting work and drains its FIFO before removing the runner.
void setRunner(Runner* runner);
bool run(const std::function<void()>& work);
void retire(VSTPluginInstance* instance) noexcept;
}
