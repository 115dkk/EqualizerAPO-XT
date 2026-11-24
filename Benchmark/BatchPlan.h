/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

// Benchmark times only whole batches of the requested size, so every measured
// batch is comparable with every other. The frames after the last whole batch
// (the tail) are still processed and written to the output file, once, after
// the timed batches, as one batch padded with silence: an impulse response
// rendered through Benchmark keeps its end.
struct BenchmarkBatchPlan
{
	unsigned timedFrames;
	unsigned tailFrames;
};

constexpr BenchmarkBatchPlan planBenchmarkBatches(unsigned frameCount, unsigned batchSize)
{
	if (batchSize == 0)
		return { 0, frameCount };
	const unsigned timed = frameCount - frameCount % batchSize;
	return { timed, frameCount - timed };
}
