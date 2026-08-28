/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <exception>
#include <utility>

class AnalysisWorkerRecovery
{
public:
	template <typename Work, typename Failure>
	static bool run(Work&& work, Failure&& failure)
	{
		try
		{
			std::forward<Work>(work)();
			return true;
		}
		catch (const std::exception& error)
		{
			std::forward<Failure>(failure)(error.what());
		}
		catch (...)
		{
			std::forward<Failure>(failure)("non-standard exception");
		}
		return false;
	}
};
