/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>
#include "ConvolverBank.h"
#include "runtime/concurrency/FanOutGovernor.h"
#include "runtime/concurrency/RealtimeFanOut.h"

// Prepared storage is owned by the filter. A negative input mapping skips a
// fresh virtual channel, preserving its previous (unadvanced) convolver state.
struct ConvolverProcessJob
{
	ConvolverBank& bank;
	double** input = nullptr;
	double** output = nullptr;
	const int* inputChannels = nullptr;
};

// Estimated spectral samples per block: units * B * (head + tail partitions),
// the same bandwidth model as hcInitPartitioned's factor selection. Benchmark
// calibration on 2026-11-19 (three runs, median of process averages in us):
// 2 units / 1024 taps / B=64: inline 1.93, parallel 4.07 (1280 work);
// 8 / 8192 / 64: 12.64 vs 14.07 (15360); 8 / 32768 / 64:
// 26.96 vs 19.71 (31744); 22 / 1024 / 480: 84.09 vs 38.47 (31680).
// 24576 conservatively excludes the first two, admits the latter two. It also
// keeps 2 / 96000 / 64 inline (7.16 vs 8.90), but admits the same IR at
// B=480 (37.50 vs 24.49). No extra block of latency is introduced.
inline bool useConvolverFanOut(unsigned units, unsigned irFrames, const HConvPartitioned& prototype) noexcept
{
	const uint64_t block = static_cast<unsigned>(prototype.framelength);
	uint64_t partitions = static_cast<unsigned>(prototype.head.num_filterbuf);
	if (prototype.tailfactor > 0)
	{
		const uint64_t tailLength = block * static_cast<unsigned>(prototype.tailfactor);
		const uint64_t tailFrames = irFrames - partitions * block;
		partitions += (tailFrames + tailLength - 1) / tailLength;
	}
	return units >= 2 && static_cast<uint64_t>(units) * block * partitions >= 24576;
}

#pragma AVRT_CODE_BEGIN
inline void processConvolverUnit(void* context, size_t index) noexcept
{
	auto& job = *static_cast<ConvolverProcessJob*>(context);
	const int inputChannel = job.inputChannels == nullptr
		? static_cast<int>(index) : job.inputChannels[index];
	if (inputChannel < 0)
		return;
	HConvPartitioned* unit = job.bank.unit(static_cast<unsigned>(index));
	hcPutPartitioned(unit, job.input[inputChannel]);
	hcProcessPartitioned(unit);
	hcGetPartitioned(unit, job.output[index]);
}

inline void processConvolverUnits(ConvolverProcessJob& job, unsigned count, bool parallel,
	eapo::rt::FanOutGovernor& governor) noexcept
{
	if (parallel)
		eapo::rt::FanOut::run(count, processConvolverUnit, &job, governor);
	else
		for (unsigned i = 0; i < count; ++i)
			processConvolverUnit(&job, i);
}
#pragma AVRT_CODE_END
