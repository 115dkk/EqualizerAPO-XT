/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The routing a new SubwooferRouting line starts with, built from the
	device's channel names. It is what ends up in the user's configuration,
	so it lives here, widget-free, where EditorLogicTests can pin it; it used
	to sit in the card editor's Q_OBJECT translation unit, which made the
	model code depend on a widget file and left the default to the pixel
	gates alone (audit #348 TD-30).
*/

#pragma once

#include <string>
#include <vector>

#include "SubwooferRouting/State.h"

namespace subwooferroutingeditor
{
// The front pair (L and R when the device has them, otherwise its first two
// non-LFE channels) as two main paths, and, when the device has an LFE
// channel, a bass path summing the pair into it beside the source LFE, with
// an 80 Hz Butterworth crossover on both sides. A device with fewer than two
// usable main channels gets a plain L/R layout. Channel names that are not
// valid stable ids, and repeats, are dropped.
subroute::SubwooferRoutingState buildDefaultState(
	const std::vector<std::wstring>& deviceChannels);

// "LFE" in any letter case.
bool isLfeChannelId(const std::string& id);
}
