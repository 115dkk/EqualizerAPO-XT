/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <mutex>

// The one answer to "how is FFTW plan creation safe in this process"
// (audit #275 A9; the pieces used to be spread over four files):
//
// 1. Every plan is created while holding a Session, whose constructor takes
//    the process-wide planner mutex below. libHybridConv holds one around
//    hcInitSingle's plans, GraphicEQFilter around its IR synthesis plans,
//    and the Editor's AnalysisThread around its response FFT plan.
// 2. The first Session ever constructed also installs FFTW's own internal
//    planner lock (ensurePlannerThreadSafe), which additionally covers plan
//    *destruction* - unique_ptr deleters run outside any Session - and any
//    future path that forgets rule 1.
// 3. The Session constructor imports cached wisdom once per process, and
//    exportWisdomForLength persists what FFTW_MEASURE learned, so later
//    processes plan fast. Callers keep their own flags: the convolution
//    path measures (Session::flags()), GraphicEQ and the analysis FFT stay
//    on FFTW_ESTIMATE.
class FftwPlanningPolicy
{
public:
	// Installs FFTW's internal planner lock, once per process. Called by
	// every Session constructor; the Editor also calls it explicitly at
	// startup before its threads exist (the historical fix for the flaky
	// startup crashes - see the comment in Editor/main.cpp).
	static void ensurePlannerThreadSafe();

	class Session
	{
	public:
		Session();
		unsigned flags() const;
		bool exportWisdomForLength(int transformLength);

	private:
		std::unique_lock<std::mutex> plannerLock;
	};
};
