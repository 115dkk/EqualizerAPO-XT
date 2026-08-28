/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

struct BenchmarkBatchPlan
{
	unsigned processedFrames;
	unsigned trimmedFrames;
};

constexpr BenchmarkBatchPlan planBenchmarkBatches(unsigned frameCount, unsigned batchSize)
{
	if (batchSize == 0)
		return { 0, frameCount };
	const unsigned processed = frameCount - frameCount % batchSize;
	return { processed, frameCount - processed };
}
