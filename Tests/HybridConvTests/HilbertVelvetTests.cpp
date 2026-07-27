/*
	This file is part of EqualizerAPO-XT.

	Regression coverage for the built-in Hilbert transform and sparse
	velvet-noise decorrelator. The command tests pin the user-facing syntax;
	the DSP tests pin the phase sign, FIR normalization, deterministic stream
	semantics, channel decorrelation and smooth dynamic renewal.
*/

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#include "filters/HilbertCommand.h"
#include "filters/HilbertFilter.h"
#include "filters/VelvetCommand.h"
#include "filters/velvet/Processor.h"
#include "Tests/TestHarness.h"

namespace
{
constexpr double Pi = 3.1415926535897932384626433832795;
test::Harness harness("HilbertVelvetTests");

std::complex<double> responseAt(const std::vector<double>& taps, double omega)
{
	std::complex<double> response;
	for (std::size_t i = 0; i < taps.size(); ++i)
		response += taps[i] * std::exp(std::complex<double>(
			0.0, -omega * static_cast<double>(i)));
	return response;
}

void testHilbertCommandContract()
{
	HilbertCommand command;
	std::wstring error;
	harness.require(HilbertCommand::parse(L"Hilbert",
		L"Shift=SL,SR Align=L,R Direction=+90", command, &error),
		"Hilbert accepts explicit shifted/aligned roles");
	harness.expectTrue(command.serialize()
		== std::wstring(L"Shift=SL,SR Align=L,R Direction=+90"),
		"Hilbert serializes a canonical role assignment");

	HilbertCommand roundTrip;
	harness.expectTrue(HilbertCommand::parse(
		L"Hilbert", command.serialize(), roundTrip, &error),
		"Hilbert canonical output parses again");
	harness.expectEqual(roundTrip.directionDegrees, 90,
		"Hilbert preserves the selected phase direction");

	harness.expectFalse(HilbertCommand::parse(L"Hilbert",
		L"Shift=L,R Align=R Direction=-90", command, &error),
		"Hilbert rejects a channel assigned to both roles");
	harness.expectFalse(HilbertCommand::parse(L"Hilbert",
		L"Shift=ALL Align=L Direction=-90", command, &error),
		"Hilbert rejects alignment beside Shift=ALL");
	harness.expectFalse(HilbertCommand::parse(L"Hilbert",
		L"Shift=L Direction=45", command, &error),
		"Hilbert accepts only the two implemented phase directions");
}

void testHilbertFirContract()
{
	const std::vector<double> minus = designHilbertFir(-90);
	const std::vector<double> plus = designHilbertFir(90);
	harness.requireEqual(minus.size(),
		static_cast<std::size_t>(HilbertTapCount),
		"Hilbert FIR has the documented odd tap count");
	harness.expectTrue(std::abs(minus[HilbertLatencySamples]) < 1.0e-15,
		"Hilbert Type III FIR has a zero centre tap");

	double maximumMirrorError = 0.0;
	double maximumDirectionError = 0.0;
	for (std::size_t i = 0; i < minus.size(); ++i)
	{
		maximumMirrorError = std::max(maximumMirrorError,
			std::abs(minus[i] + minus[minus.size() - 1 - i]));
		maximumDirectionError = std::max(maximumDirectionError,
			std::abs(minus[i] + plus[i]));
	}
	harness.expectTrue(maximumMirrorError < 1.0e-14,
		"Hilbert FIR remains antisymmetric");
	harness.expectTrue(maximumDirectionError < 1.0e-14,
		"the two phase directions are exact sign inverses");

	const std::complex<double> minusMid = responseAt(minus, Pi * 0.5);
	const std::complex<double> plusMid = responseAt(plus, Pi * 0.5);
	harness.expectTrue(std::abs(std::abs(minusMid) - 1.0) < 1.0e-12,
		"Hilbert mid-band magnitude is normalized to 0 dB");
	harness.expectTrue(minusMid.imag() < -0.999999999
		&& std::abs(minusMid.real()) < 1.0e-10,
		"Direction=-90 produces the negative-quadrature response");
	harness.expectTrue(plusMid.imag() > 0.999999999
		&& std::abs(plusMid.real()) < 1.0e-10,
		"Direction=+90 produces the positive-quadrature response");
}

void testVelvetCommandContract()
{
	VelvetCommand command;
	std::wstring error;
	harness.require(VelvetCommand::parse(L"Velvet",
		L"Mode=Static Amount=75% Length=20ms Density=1200/s Evolution=2s "
		L"Transition=100ms Decay=-48dB Variation=42",
		command, &error),
		"Velvet accepts every documented parameter");
	harness.expectFalse(command.parameters.dynamic,
		"Velvet preserves Static mode");
	harness.expectTrue(std::abs(command.parameters.amount - 0.75) < 1.0e-12,
		"Velvet parses Amount as a linear mix");

	VelvetCommand roundTrip;
	harness.expectTrue(VelvetCommand::parse(
		L"Velvet", command.serialize(), roundTrip, &error),
		"Velvet canonical output parses again");
	harness.expectEqual(roundTrip.parameters.seed,
		static_cast<std::uint64_t>(42),
		"Velvet preserves the deterministic variation");

	harness.expectFalse(VelvetCommand::parse(L"Velvet",
		L"Amount=101%", command, &error),
		"Velvet rejects an out-of-range mix");
	harness.expectFalse(VelvetCommand::parse(L"Velvet",
		L"Evolution=0.1s Transition=100ms", command, &error),
		"Velvet rejects a transition longer than 90% of its evolution period");
	harness.expectFalse(VelvetCommand::parse(L"Velvet",
		L"Variation=0", command, &error),
		"Velvet rejects the reserved zero variation");
}

void testVelvetIsNormalizedAndDecorrelated()
{
	velvet::Processor processor;
	harness.require(processor.prepare(48000.0, 4),
		"Velvet prepares four channels");
	velvet::Parameters parameters;
	parameters.dynamic = false;
	parameters.seed = 0xc001d00dULL;
	harness.require(processor.setParameters(parameters),
		"Velvet accepts its documented static parameters");

	const velvet::Statistics statistics = processor.statistics();
	harness.expectTrue(statistics.tapsPerChannel >= 29
		&& statistics.tapsPerChannel <= 31,
		"default Velvet density produces about 30 taps per channel");
	harness.expectEqual(processor.activeTapCount(0),
		statistics.tapsPerChannel,
		"Velvet exposes exactly the active sparse taps");
	harness.expectTrue(std::abs(statistics.minimumEnergy - 1.0) < 1.0e-12
		&& std::abs(statistics.maximumEnergy - 1.0) < 1.0e-12,
		"every Velvet kernel has unit energy");
	harness.expectTrue(statistics.maximumZeroLagCorrelation < 0.8,
		"independent channel kernels remain decorrelated");
}

void testVelvetStreamIsBlockSizeInvariant()
{
	velvet::Parameters parameters;
	parameters.seed = 123456789;
	parameters.refreshSeconds = 0.02;
	parameters.transitionMs = 5.0;

	velvet::Processor whole;
	velvet::Processor split;
	harness.require(whole.prepare(48000.0, 2)
		&& split.prepare(48000.0, 2),
		"Velvet prepares matching processors");
	harness.require(whole.setParameters(parameters)
		&& split.setParameters(parameters),
		"Velvet accepts fast-renewal test parameters");

	constexpr std::size_t Frames = 4096;
	std::vector<double> left(Frames);
	std::vector<double> right(Frames);
	for (std::size_t i = 0; i < Frames; ++i)
	{
		left[i] = std::sin(static_cast<double>(i) * 0.031);
		right[i] = std::cos(static_cast<double>(i) * 0.019);
	}
	std::vector<double> wholeLeft(Frames);
	std::vector<double> wholeRight(Frames);
	std::vector<double> splitLeft(Frames);
	std::vector<double> splitRight(Frames);
	const double* wholeInput[] = {left.data(), right.data()};
	double* wholeOutput[] = {wholeLeft.data(), wholeRight.data()};
	whole.process(wholeOutput, wholeInput, Frames);

	const std::size_t blocks[] = {17, 31, 64, 127, 5, 509};
	std::size_t offset = 0;
	std::size_t block = 0;
	while (offset < Frames)
	{
		const std::size_t count = std::min(
			blocks[block++ % 6], Frames - offset);
		const double* input[] = {
			left.data() + offset, right.data() + offset
		};
		double* output[] = {
			splitLeft.data() + offset, splitRight.data() + offset
		};
		split.process(output, input, count);
		offset += count;
	}

	double maximumDifference = 0.0;
	for (std::size_t i = 0; i < Frames; ++i)
	{
		maximumDifference = std::max(maximumDifference,
			std::abs(wholeLeft[i] - splitLeft[i]));
		maximumDifference = std::max(maximumDifference,
			std::abs(wholeRight[i] - splitRight[i]));
	}
	harness.expectTrue(maximumDifference < 1.0e-12,
		"Velvet output does not depend on host block boundaries");
}

void testVelvetDynamicRenewalIsFiniteAndSmooth()
{
	velvet::Processor processor;
	harness.require(processor.prepare(48000.0, 1),
		"Velvet prepares a mono transition test");
	velvet::Parameters parameters;
	parameters.refreshSeconds = 0.01;
	parameters.transitionMs = 5.0;
	parameters.seed = 42;
	harness.require(processor.setParameters(parameters),
		"Velvet accepts dynamic transition parameters");

	constexpr std::size_t Frames = 4096;
	std::vector<double> input(Frames, 1.0);
	std::vector<double> output(Frames);
	const double* inputs[] = {input.data()};
	double* outputs[] = {output.data()};
	processor.process(outputs, inputs, Frames);

	double largestStep = 0.0;
	for (std::size_t i = 1; i < Frames; ++i)
	{
		harness.require(std::isfinite(output[i]),
			"Velvet dynamic output remains finite");
		largestStep = std::max(largestStep,
			std::abs(output[i] - output[i - 1]));
	}
	harness.expectTrue(largestStep < 2.0,
		"equal-power renewal avoids discontinuous kernel replacement");
}
}

void runHilbertVelvetTests()
{
	testHilbertCommandContract();
	testHilbertFirContract();
	testVelvetCommandContract();
	testVelvetIsNormalizedAndDecorrelated();
	testVelvetStreamIsBlockSizeInvariant();
	testVelvetDynamicRenewalIsFiniteAndSmooth();
	harness.report();
}
