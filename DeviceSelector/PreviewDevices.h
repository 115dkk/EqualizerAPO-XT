/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Canned AbstractAPOInfo endpoints for the --skin-shots harness: a
	deterministic device mix (installed, installable, default,
	unplugged) with no registry or COM access, so every skin's dialog can be
	rendered offscreen and byte-compared across machines and CI runs.
*/

#pragma once

#include <memory>
#include <vector>

#include <devices/AbstractAPOInfo.h>

namespace PreviewDevices
{
std::vector<std::shared_ptr<AbstractAPOInfo>> playback();
std::vector<std::shared_ptr<AbstractAPOInfo>> capture();
}
