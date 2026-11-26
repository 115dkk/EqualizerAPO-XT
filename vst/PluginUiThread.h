/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>

class VSTPluginInstance;

namespace PluginUiThread
{
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
