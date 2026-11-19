/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <windows.h>

// Stack kept free below the guard page for handling a stack overflow, on a
// thread that is about to call into a plug-in.
//
// The SEH guards around plug-in calls catch a plug-in's stack overflow, but
// Windows first has to write the exception record and the full processor
// context onto the overflowing thread's own stack. By default about one page
// is left for that. The context grows with the CPU's extended state: it fits
// on a CPU with AVX2, and no longer fits on one with AVX-512 or AMX state.
// There the dispatch itself faults and Windows terminates the process with
// 0xC0000005 before any handler runs, so the guard never sees the overflow.
// Vst3HostTests::testConsecutiveStackOverflowsAreContained died this way on
// some CI runners and never on others.
constexpr ULONG pluginCallStackGuaranteeBytes = 64 * 1024;

// Once per thread; later calls cost one thread-local read. The first call on
// a thread makes one system call. A thread whose stack is too small for the
// guarantee keeps the default; the call fails without side effects.
inline void ensurePluginCallStackGuarantee() noexcept
{
	thread_local bool requested = false;
	if (requested)
		return;
	requested = true;
	ULONG bytes = pluginCallStackGuaranteeBytes;
	SetThreadStackGuarantee(&bytes);
}
