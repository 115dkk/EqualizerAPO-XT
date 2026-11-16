/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <atomic>
#include <cstdio>
#include <intrin.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "runtime/concurrency/ShutdownSequence.h"
#include "services/logging/Logging.h"

namespace ProcessShutdown
{
// Until main has a result, a stuck worker is a failure (including a gate that
// has not reached its verdict). Destructors keep a result already chosen by main.
inline std::atomic<int> exitCode{1};

[[noreturn]] inline void hardExit(int code)
{
	// Logging::log closes/flushes its destination before returning. Flush only
	// our console streams, not arbitrary streams owned by a stuck plug-in.
	std::fflush(stdout);
	std::fflush(stderr);
	// A third-party DllMain may never release the loader lock. ExitProcess
	// would deadlock in DLL_PROCESS_DETACH; do not run static destruction or
	// detach a worker and continue. testShutdownSequenceTimeoutSkipsApplyAndLaterWaits,
	// audit #467 TD-58. The whole process is the only safe lifetime boundary.
	TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
	__fastfail(FAST_FAIL_FATAL_APP_EXIT); // A self TerminateProcess cannot return on success.
}

inline void run(std::initializer_list<ShutdownSequence::Wait> waits,
	const std::function<void()>& applyUpdate = [] {})
{
	ShutdownSequence::run(waits, exitCode.load(std::memory_order_relaxed), applyUpdate,
		[](const std::wstring& line) { LogFStatic(L"%s", line.c_str()); }, hardExit);
}
}
