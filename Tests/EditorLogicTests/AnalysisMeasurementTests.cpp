/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include <QString>

#include "Editor/analysis/AnalysisRequestFence.h"
#include "Editor/analysis/ImpulseMeasurement.h"

namespace
{
constexpr unsigned kSampleRate = 48000;
constexpr int kResponseFrames = 256;
constexpr int kBlockFrames = 64;
constexpr unsigned kChannelCount = 2;
constexpr int kChannelIndex = 1;

// Stands in for FilterEngine: every channel is delayed by delayFrames and
// scaled by gain; an optional second tap adds tailGain * x[n - tailFrames]
// behind it. Interleaved, as FilterEngine::process() is.
class FakeProcessor
{
public:
	FakeProcessor(int delayFrames, double gain, int tailFrames = 0, double tailGain = 0.0)
		: delayFrames(delayFrames), gain(gain), tailFrames(tailFrames), tailGain(tailGain)
	{
	}

	void process(double* output, const double* input, int frameCount)
	{
		for (int frame = 0; frame < frameCount; frame++)
		{
			for (unsigned channel = 0; channel < kChannelCount; channel++)
				history.push_back(input[frame * kChannelCount + channel]);

			const long now = static_cast<long>(history.size() / kChannelCount) - 1;
			for (unsigned channel = 0; channel < kChannelCount; channel++)
			{
				double value = gain * past(now - delayFrames, channel);
				if (tailGain != 0.0)
					value += tailGain * past(now - delayFrames - tailFrames, channel);
				output[frame * kChannelCount + channel] = value;
			}
		}
	}

private:
	double past(long frame, unsigned channel) const
	{
		return frame < 0 ? 0.0 : history[static_cast<size_t>(frame) * kChannelCount + channel];
	}

	int delayFrames;
	double gain;
	int tailFrames;
	double tailGain;
	std::vector<double> history;
};

struct MeasurementRun
{
	bool found = false;
	int startFrame = -1;
	int latencyFrames = 0;
	unsigned processedFrames = 0;
	std::vector<double> aligned;
};

// The loop AnalysisThread::run() drives: a one-frame impulse on every channel,
// then silence, block by block until the measurement completes or ten seconds
// of audio have gone by.
MeasurementRun measure(FakeProcessor& processor, int responseFrames = kResponseFrames,
	int blockFrames = kBlockFrames)
{
	MeasurementRun run;
	run.aligned.assign(responseFrames, 0.0);
	std::vector<double> input(static_cast<size_t>(blockFrames) * kChannelCount, 0.0);
	std::vector<double> output(input.size(), 0.0);
	for (unsigned i = 0; i < kChannelCount; i++)
		input[i] = 1.0;

	ImpulseMeasurement measurement(
		kChannelCount, kChannelIndex, responseFrames, blockFrames, run.aligned.data());
	while (run.processedFrames < 10 * kSampleRate)
	{
		processor.process(output.data(), input.data(), blockFrames);
		run.processedFrames += blockFrames;
		if (measurement.addBlock(output.data()))
			break;
		if (run.processedFrames == static_cast<unsigned>(blockFrames))
		{
			for (unsigned i = 0; i < kChannelCount; i++)
				input[i] = 0.0;
		}
	}

	run.found = measurement.found();
	run.startFrame = measurement.startFrame();
	run.latencyFrames = measurement.latencyFrames();
	return run;
}

// A plain DFT of the aligned response, laid out the way FFTW's r2c writes
// it (frameCount / 2 + 1 bins, Nyquist included).
std::vector<std::complex<double>> spectrum(const std::vector<double>& samples)
{
	const size_t size = samples.size();
	std::vector<std::complex<double>> bins(size / 2 + 1);
	for (size_t bin = 0; bin < bins.size(); bin++)
	{
		std::complex<double> sum(0.0, 0.0);
		for (size_t n = 0; n < size; n++)
		{
			const double angle = -2.0 * std::numbers::pi_v<double>
				* static_cast<double>(bin * n) / static_cast<double>(size);
			sum += samples[n] * std::polar(1.0, angle);
		}
		bins[bin] = sum;
	}
	return bins;
}
}

// Audit #348 F6/TD-37: the thread used to decide freshness with three
// hand-written comparisons, and the test pinned only the one-line equality
// they shared, so deleting any one of them stayed green. The thread's three
// publish points (the result, the failure, the finished signal) are modelled
// as three publishIf() calls here.
void testAnalysisRequestFence()
{
	AnalysisRequestFence fence;

	const AnalysisRequestFence::Ticket first = fence.begin();
	expectTrue(fence.isCurrent(first), "a fresh ticket is current");

	int currentPublished = 0;
	expectTrue(fence.publishIf(first, [&] { currentPublished++; }),
		"the current ticket publishes");
	expectEqual(currentPublished, 1, "the current ticket runs its action once");

	const AnalysisRequestFence::Ticket second = fence.begin();
	expectFalse(fence.isCurrent(first), "a newer begin() supersedes the earlier ticket");
	expectTrue(fence.isCurrent(second), "the newest ticket is current");

	int resultPublished = 0;
	int failurePublished = 0;
	int finishedEmitted = 0;
	expectFalse(fence.publishIf(first, [&] { resultPublished++; }),
		"a superseded ticket is refused at the result publish point");
	expectFalse(fence.publishIf(first, [&] { failurePublished++; }),
		"a superseded ticket is refused at the failure publish point");
	expectFalse(fence.publishIf(first, [&] { finishedEmitted++; }),
		"a superseded ticket is refused at the finished-signal point");
	expectEqual(resultPublished + failurePublished + finishedEmitted, 0,
		"a superseded ticket publishes nothing");

	int secondPublished = 0;
	expectTrue(fence.publishIf(second, [&] { secondPublished++; }),
		"the newest ticket still publishes");
	expectEqual(secondPublished, 1, "the newest ticket runs its action once");
}

// The response is four processing blocks long. A start in the first block must
// keep copying until all 256 response samples are present.
void testImpulseMeasurementFindsTheDelay()
{
	{
		FakeProcessor processor(16, 1.0, 104, 0.25);
		const MeasurementRun run = measure(processor);

		requireTrue(run.found, "the delayed impulse is found in the first sub-block");
		expectEqual(run.latencyFrames, 16, "the absolute latency includes the first-block offset");
		expectEqual(run.startFrame, 16, "the block-relative start stays meaningful");
		expectEqual(static_cast<int>(run.processedFrames), 5 * kBlockFrames,
			"the response spans the tail of the first block and four later blocks");

		expectTrue(run.aligned[0] == 1.0, "the aligned response begins at the impulse");
		expectTrue(run.aligned[104] == 0.25,
			"a later tap lands at its response-relative frame across sub-blocks");
		int nonZero = 0;
		for (double sample : run.aligned)
			nonZero += sample != 0.0 ? 1 : 0;
		expectEqual(nonZero, 2, "nothing else lands in the aligned response");
	}

	{
		FakeProcessor processor(2 * kBlockFrames + 7, 1.0);
		const MeasurementRun run = measure(processor);

		requireTrue(run.found, "the impulse is found after two silent sub-blocks");
		expectEqual(run.startFrame, 7, "the later start keeps its block-relative offset");
		expectEqual(run.latencyFrames, 2 * kBlockFrames + 7,
			"latency counts every preceding silent sub-block");
		expectEqual(static_cast<int>(run.processedFrames), 7 * kBlockFrames,
			"later discovery still collects a full response");
	}
}

void testImpulseMeasurementAtTheBlockStart()
{
	{
		FakeProcessor processor(2 * kBlockFrames, 1.0);
		const MeasurementRun run = measure(processor);

		requireTrue(run.found, "an impulse exactly on a later block boundary is found");
		expectEqual(run.startFrame, 0, "a boundary start has no within-block offset");
		expectEqual(run.latencyFrames, 2 * kBlockFrames,
			"a boundary start reports the absolute output frame");
		expectEqual(static_cast<int>(run.processedFrames), 6 * kBlockFrames,
			"four response blocks follow the two silent blocks");
	}

	{
		FakeProcessor processor(480, 1.0, 40, 0.25);
		const MeasurementRun run = measure(processor, kResponseFrames, kResponseFrames);

		requireTrue(run.found, "the old whole-block measurement still finds the delay");
		expectEqual(run.latencyFrames, 480, "the whole-block latency stays unchanged");
		expectEqual(run.startFrame, 224, "the whole-block start stays unchanged");
		expectEqual(static_cast<int>(run.processedFrames), 3 * kResponseFrames,
			"the old block after the start still completes the response");
		expectTrue(run.aligned[0] == 1.0, "the whole-block response begins at the impulse");
		expectTrue(run.aligned[40] == 0.25, "the whole-block response keeps the later tap");
	}
}

void testImpulseMeasurementWithoutAnImpulse()
{
	FakeProcessor processor(0, 0.0);
	const MeasurementRun run = measure(processor);

	expectFalse(run.found, "silence holds no impulse");
	expectEqual(run.latencyFrames, 0, "no impulse reports no latency");
	expectTrue(run.processedFrames >= 10 * kSampleRate,
		"the search gives up after ten seconds of audio");
}

// A gain of one half: the -6.02 dB of a "-6 dB" preamp in the classic sense.
// The peak includes the Nyquist bin, as the graph does (audit #348).
void testImpulsePeakGain()
{
	FakeProcessor processor(480, 0.5);
	const MeasurementRun run = measure(processor);
	requireTrue(run.found, "the scaled impulse is found");

	const std::vector<std::complex<double>> bins = spectrum(run.aligned);
	std::vector<double> interleaved;
	interleaved.reserve(bins.size() * 2);
	for (const std::complex<double>& bin : bins)
	{
		interleaved.push_back(bin.real());
		interleaved.push_back(bin.imag());
	}
	const double peakDb = impulsePeakGainDb(
		reinterpret_cast<const double (*)[2]>(interleaved.data()), bins.size());
	expectTrue(std::abs(peakDb - (-6.0206)) < 0.01,
		QStringLiteral("a gain of 0.5 peaks at -6.02 dB, got %1").arg(peakDb, 0, 'f', 4));

	// Only the Nyquist bin carries the peak: dropping it must lower the result.
	std::vector<double> nyquistOnly(bins.size() * 2, 0.0);
	nyquistOnly[(bins.size() - 1) * 2] = 2.0;
	nyquistOnly[0] = 1.0;
	const double withNyquist = impulsePeakGainDb(
		reinterpret_cast<const double (*)[2]>(nyquistOnly.data()), bins.size());
	expectTrue(std::abs(withNyquist - 20.0 * std::log10(2.0)) < 1.0e-12,
		"a peak in the Nyquist bin counts");
}
