/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <algorithm>

namespace servicecontrol
{
// A shared absolute deadline bounds the entire restart, not each transition.
// Clock and pause are injected so tests never touch the SCM or wait in real time.
template<class State, class Tick, class Query, class Clock, class Pause>
bool waitFor(State target, Tick deadline, Query query, Clock clock, Pause pause)
{
	while (query() != target)
	{
		const Tick now = clock();
		if (now >= deadline)
			return false;
		pause((std::min)(deadline - now, Tick(100)));
	}
	return true;
}
}
