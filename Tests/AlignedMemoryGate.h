/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The leak canary behind CI's memcheck gate. Every engine audio buffer -
	filter state, convolution history, VST slot buffers - is allocated
	through AlignedMemory, so a suite that ends with more alloc() than
	free() calls has leaked a buffer the way the original convolution-tail
	bug class does. MSVC's AddressSanitizer has no LeakSanitizer on Windows;
	this covers the engine-buffer class of leaks deterministically instead,
	in every build flavor.

	Call from a suite binary's main() after every harness reported: by then
	all filters and configurations must be destroyed, so the counters must
	balance. The check is direction-agnostic (a mid-run counter reset while
	buffers were live would show as more frees than allocations - that is a
	test-hygiene bug worth failing on too).
*/

#pragma once

#include <cstdio>

#include "runtime/memory/AlignedMemory.h"

namespace test
{
inline int reportAlignedMemoryBalance(const char* binaryName)
{
	const size_t allocated = AlignedMemory::allocationCountForTesting();
	const size_t freed = AlignedMemory::freeCountForTesting();
	if (allocated == freed)
	{
		std::printf("%s: aligned-memory balance OK (%zu allocations, all freed)\n",
			binaryName, allocated);
		return 0;
	}
	if (allocated > freed)
		std::fprintf(stderr, "%s: MEMLEAK: %zu aligned buffer(s) still allocated at exit"
			" (%zu allocated, %zu freed)\n",
			binaryName, allocated - freed, allocated, freed);
	else
		std::fprintf(stderr, "%s: aligned-memory counters are skewed: %zu more frees than"
			" allocations (a counter reset while buffers were live?)\n",
			binaryName, freed - allocated);
	return 1;
}
}
