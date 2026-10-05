/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The analysis panel's impulse measurement, out of AnalysisThread::run()
	(audit #348 F6/TD-37): find where the processed impulse starts in the
	analysis channel, count the latency before it, and assemble the response
	aligned to that start across however many processing blocks it needs. Then
	the peak gain of the transformed response.

	Qt-free and FFTW-free: the caller runs the engine one block at a time,
	hands each processed block in, and owns the buffer the aligned response is
	written to (the FFT input) and the transform itself.
*/

#pragma once

#include <cstddef>

class ImpulseMeasurement
{
public:
	// Processed blocks are interleaved, channelCount samples per frame and
	// blockFrames frames; channelIndex picks the analysis channel.
	// alignedResponse must hold responseFrames samples and outlive the
	// measurement.
	ImpulseMeasurement(
		unsigned channelCount,
		int channelIndex,
		int responseFrames,
		int blockFrames,
		double* alignedResponse);

	// Takes the next fixed-size processed block and answers whether the
	// measurement is complete. Once the impulse starts, every remaining sample
	// in that block and following blocks is copied until responseFrames samples
	// have been assembled.
	bool addBlock(const double* processed);

	// Whether the impulse start has been seen.
	bool found() const;
	// The frame within the processing block at which the impulse starts; -1
	// before it is found. latencyFrames() carries the absolute frame index.
	int startFrame() const;
	// Frames from the impulse to its start in the output: every block that
	// held no start, plus the start frame. 0 while nothing was found.
	int latencyFrames() const;

private:
	unsigned channelCount;
	int channelIndex;
	int responseFrames;
	int blockFrames;
	double* alignedResponse;
	int start = -1;
	int silentFrames = 0;
	int copiedFrames = 0;
};

// The largest magnitude over every bin, in dB. bins are FFTW's layout
// (re, im) pairs; the Nyquist bin counts, as the graph draws it (audit #348).
double impulsePeakGainDb(const double (*bins)[2], std::size_t binCount);
