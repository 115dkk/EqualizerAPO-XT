/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026  115dkk

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <cstddef>
#include <functional>

// Bounded parallel work for configuration-time preparation. This helper is
// deliberately not an AVRT facility: it may allocate, create threads and
// propagate exceptions. Callers use it only while building immutable DSP/UI
// state, then hand borrowed pointers to the real-time path.
class ParallelExecutor
{
public:
	using Operation = std::function<void(size_t)>;

	// Executes every index in [0, taskCount) exactly once unless an operation
	// throws. The caller participates as one worker; the first exception stops
	// new work, all created workers are joined, and the exception is rethrown.
	// maxConcurrency == 0 selects a bounded hardware-derived default.
	static void forEach(size_t taskCount, const Operation& operation, unsigned maxConcurrency = 0);

private:
	static unsigned concurrencyFor(size_t taskCount, unsigned maxConcurrency) noexcept;
};
