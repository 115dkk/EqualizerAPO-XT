/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

class IRegistry;

namespace ResidentHostLauncher
{
// Starts the host now when a playback endpoint receives Send audio. Missing
// installation files or an existing session host need no launch; an existing
// host picks up receivers itself. Never throws. Returns one line for the log.
std::wstring ensureStarted(const IRegistry& registry);
std::wstring ensureStarted();
}
