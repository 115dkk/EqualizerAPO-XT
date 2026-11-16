/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>
#include <initializer_list>
#include <string>

namespace ShutdownSequence
{
struct Wait
{
	// Read after a timeout, so a loader reports the library it actually entered.
	std::function<std::wstring()> name;
	std::function<bool()> finish;
};

inline bool run(std::initializer_list<Wait> waits, int exitCode,
	const std::function<void()>& applyUpdate,
	const std::function<void(const std::wstring&)>& log,
	const std::function<void(int)>& hardExit)
{
	for (const Wait& wait : waits)
	{
		if (!wait.finish())
		{
			log(L"Shutdown timed out: " + wait.name());
			hardExit(exitCode);
			// The injected exit returns in tests only; never attempt later waits
			// or apply the update after this decision (audit #467 TD-58).
			return false;
		}
	}
	applyUpdate();
	return true;
}
}
