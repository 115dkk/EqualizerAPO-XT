/*
	This file is part of EqualizerAPO-XT.

	One analysis run's frequency response, in the form the analyzer actually
	produced it.
*/

#pragma once

#include <complex>
#include <cstddef>
#include <vector>

// The analyzer FFTs a real signal, so of the fftSize complex bins only the
// first fftSize / 2 + 1 carry information; the rest are the conjugate mirror,
// and FFTW's real-to-complex transform never writes them at all. Keeping only
// that half halves the memory at every resolution setting and removes a read of
// uninitialized memory that the previous whole-buffer copy performed on every
// run.
//
// The bins stay complex on purpose. Taking the magnitude here would throw the
// phase away, and phase is the entire subject of an all-pass or a delay: a
// config that only shifts phase is a flat line in a magnitude plot. Keeping the
// complex numbers lets magnitude, phase and group delay all be derived from one
// analysis, so switching what the graph shows costs no FilterEngine run and no
// FFT.
//
// Qt-free by intent, so the analysis thread, the curve builder and the tests
// can all agree on one representation without dragging widgets into the
// dependency.
struct AnalysisResponse
{
	unsigned sampleRate = 0;
	// The analyzer's FFT length - the resolution setting, not bins.size().
	size_t fftSize = 0;
	// Leading silence the analyzer stripped off before transforming, in frames.
	// The bins therefore describe the config with its bulk delay taken out; a
	// view that wants the delay back reapplies the linear phase this implies
	// rather than asking for a second analysis.
	int latencyFrames = 0;
	// True when a time-varying filter was intentionally frozen to one
	// deterministic kernel for this frequency-response measurement.
	bool frozenDynamicResponse = false;
	// DC first, Nyquist last: binCountFor(fftSize) entries.
	std::vector<std::complex<double>> bins;

	// How many bins a real-to-complex transform of this length produces.
	static size_t binCountFor(size_t fftSize);

	bool isEmpty() const;
	size_t binCount() const;
	// Centre frequency of a bin in Hz; bin 0 is DC and the last bin is Nyquist.
	double frequencyOf(size_t index) const;
	double nyquist() const;
	// The bin whose centre frequency is nearest hz, clamped to the valid range.
	// Returns 0 for an empty response.
	size_t nearestBin(double hz) const;
	double latencySeconds() const;
};
