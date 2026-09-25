/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The analysis panel's impulse measurement, out of AnalysisThread::run()
	(audit #348 F6/TD-37): find where the processed impulse starts in the
	analysis channel, count the latency before it, and assemble the response
	aligned to that start, which may straddle two blocks. Then the peak gain
	of the transformed response.

	Qt-free and FFTW-free: the caller runs the engine one block at a time,
	hands each processed block in, and owns the buffer the aligned response is
	written to (the FFT input) and the transform itself.
*/

#pragma once

#include <cstddef>

class ImpulseMeasurement
{
public:
	// processed blocks are interleaved, channelCount samples per frame and
	// frameCount frames; channelIndex picks the analysis channel.
	// alignedResponse must hold frameCount samples and outlive the
	// measurement.
	ImpulseMeasurement(
		unsigned channelCount,
		int channelIndex,
		int frameCount,
		double* alignedResponse);

	// Takes the next processed block and answers whether the measurement is
	// complete. When the impulse starts at frame s > 0 of a block, the
	// aligned response needs the first s frames of the following block too,
	// so the block after the start completes it.
	bool addBlock(const double* processed);

	// Whether the impulse start has been seen.
	bool found() const;
	// The frame within its block at which the impulse starts; -1 before it is
	// found.
	int startFrame() const;
	// Frames from the impulse to its start in the output: every block that
	// held no start, plus the start frame. 0 while nothing was found.
	int latencyFrames() const;

private:
	unsigned channelCount;
	int channelIndex;
	int frameCount;
	double* alignedResponse;
	int start = -1;
	int silentFrames = 0;
};

// The largest magnitude over every bin, in dB. bins are FFTW's layout
// (re, im) pairs; the Nyquist bin counts, as the graph draws it (audit #348).
double impulsePeakGainDb(const double (*bins)[2], std::size_t binCount);
