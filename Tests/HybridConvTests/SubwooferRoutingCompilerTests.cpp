// SPDX-License-Identifier: MIT

#include "SubwooferRouting/Compiler.h"
#include "Tests/TestHarness.h"

#include <cmath>
#include <numbers>
#include <complex>
#include <string>
#include <vector>

namespace
{

test::Harness harness("SubwooferRoutingCompilerTests");

subroute::Path makePath(
	const std::string& id,
	subroute::PathKind kind,
	const std::string& inputChannelId)
{
	subroute::Path path;
	path.id = id;
	path.kind = kind;
	path.sourceMix.push_back({inputChannelId, 1.0});
	path.chain.push_back(subroute::PolarityStage{});
	path.chain.push_back(subroute::DelayStage{});
	path.chain.push_back(subroute::EqualizerSlotsStage{});
	return path;
}

subroute::SubwooferRoutingState makeValidState()
{
	subroute::SubwooferRoutingState state;
	state.layout.channels = {
		{"FL", "Front left"},
		{"FR", "Front right"},
		{"SUB", "Subwoofer"}};

	state.paths.push_back(
		makePath("main.fl", subroute::PathKind::Main, "FL"));
	state.paths.push_back(
		makePath("main.fr", subroute::PathKind::Main, "FR"));
	state.paths.push_back(
		makePath("bass.sub", subroute::PathKind::Bass, "FL"));

	subroute::SpeakerGroup group;
	group.id = "front";
	group.displayName = "Front";
	group.mainPathIds = {"main.fl", "main.fr"};
	group.bassPathId = "bass.sub";
	state.speakerGroups.push_back(group);

	subroute::OutputMatrixEntry left;
	left.targetChannelId = "FL";
	left.terms.push_back({"main.fl", 0.0});
	state.outputMatrix.push_back(left);

	subroute::OutputMatrixEntry right;
	right.targetChannelId = "FR";
	right.terms.push_back({"main.fr", 0.0});
	state.outputMatrix.push_back(right);

	subroute::OutputMatrixEntry subwoofer;
	subwoofer.targetChannelId = "SUB";
	subwoofer.terms.push_back({"bass.sub", 0.0});
	state.outputMatrix.push_back(subwoofer);

	return state;
}

subroute::SubwooferRoutingState makeTwoTermHeadroomState()
{
	subroute::SubwooferRoutingState state;
	state.layout.channels = {
		{"A", "Channel A"},
		{"B", "Channel B"}};

	state.paths.push_back(
		makePath("path.a", subroute::PathKind::Main, "A"));
	state.paths.push_back(
		makePath("path.b", subroute::PathKind::Main, "B"));

	subroute::SpeakerGroup group;
	group.id = "pair";
	group.displayName = "Pair";
	group.mainPathIds = {"path.a", "path.b"};
	state.speakerGroups.push_back(group);

	subroute::OutputMatrixEntry output;
	output.targetChannelId = "A";
	output.mode = subroute::OutputMode::Replace;
	output.terms.push_back({"path.a", 0.0});
	output.terms.push_back({"path.b", 0.0});
	state.outputMatrix.push_back(output);

	return state;
}

subroute::PrepareSpec makePrepareSpec()
{
	subroute::PrepareSpec spec;
	spec.sampleRate = 48000.0;
	spec.maximumBlockSize = 1024;
	spec.channelLayout = {"FL", "FR", "SUB"};
	return spec;
}

bool hasDiagnostic(
	const subroute::ValidationResult& validation,
	subroute::DiagnosticCode code)
{
	for (const subroute::ValidationDiagnostic& diagnostic :
		validation.diagnostics)
	{
		if (diagnostic.code == code)
		{
			return true;
		}
	}

	return false;
}

const subroute::CompiledStage* findStage(
	const subroute::CompiledPath& path,
	subroute::CompiledStageKind kind)
{
	for (const subroute::CompiledStage& stage : path.stages)
	{
		if (stage.kind == kind)
		{
			return &stage;
		}
	}

	return nullptr;
}

double evaluateMagnitudeDb(
	const subroute::BiquadCoefficients& coefficients,
	double frequencyHz,
	double sampleRate)
{
	const double omega = 2.0 * std::numbers::pi_v<double> * frequencyHz / sampleRate;
	const std::complex<double> z1 = std::polar(1.0, -omega);
	const std::complex<double> z2 = z1 * z1;
	const std::complex<double> numerator =
		coefficients.b0
		+ coefficients.b1 * z1
		+ coefficients.b2 * z2;
	const std::complex<double> denominator =
		1.0
		+ coefficients.a1 * z1
		+ coefficients.a2 * z2;

	return 20.0 * std::log10(std::abs(numerator / denominator));
}

void testValidStateCompiles()
{
	const subroute::CompileResult result =
		subroute::compile(makeValidState(), makePrepareSpec());

	harness.expectTrue(result.succeeded(), "valid state should compile");
	harness.expectFalse(
		result.validation.hasErrors(),
		"valid state should have no errors");
	harness.expectTrue(result.graph.has_value(), "valid state should produce a graph");
	harness.expectTrue(
		result.headroom.has_value(),
		"valid state should produce headroom analysis");
}

void testRepresentativeDiagnostics()
{
	{
		subroute::SubwooferRoutingState state = makeValidState();
		state.paths.push_back(state.paths.front());
		const subroute::ValidationResult result = subroute::validate(state);
		harness.expectTrue(
			hasDiagnostic(result, subroute::DiagnosticCode::DuplicatePathId),
			"duplicate path ID should be diagnosed");
	}

	{
		subroute::SubwooferRoutingState state = makeValidState();
		state.outputMatrix.front().terms.front().sourcePathId = "missing.path";
		const subroute::ValidationResult result = subroute::validate(state);
		harness.expectTrue(
			hasDiagnostic(
				result,
				subroute::DiagnosticCode::DanglingOutputPathReference),
			"dangling matrix path should be diagnosed");
	}

	{
		subroute::SubwooferRoutingState state = makeValidState();
		state.paths.front().kind = subroute::PathKind::Bass;
		const subroute::ValidationResult result = subroute::validate(state);
		harness.expectTrue(
			hasDiagnostic(result, subroute::DiagnosticCode::PathKindMismatch),
			"group path kind mismatch should be diagnosed");
	}

	{
		subroute::SubwooferRoutingState state = makeValidState();
		state.paths.front().chain.erase(state.paths.front().chain.begin());
		const subroute::ValidationResult result = subroute::validate(state);
		harness.expectTrue(
			hasDiagnostic(
				result,
				subroute::DiagnosticCode::MissingPolarityStage),
			"missing polarity stage should be diagnosed");
	}

	{
		subroute::SubwooferRoutingState state = makeValidState();
		subroute::BiquadFilter filter;
		filter.type = subroute::BiquadType::LowPass;
		filter.frequencyHz = 30000.0;
		filter.q = 0.7071;
		state.paths.front().chain.push_back(
			subroute::BiquadStage{filter});

		const subroute::ValidationResult result =
			subroute::validate(state, makePrepareSpec());
		harness.expectTrue(
			hasDiagnostic(
				result,
				subroute::DiagnosticCode::FrequencyAtOrAboveNyquist),
			"30 kHz cutoff should exceed Nyquist at 48 kHz");
	}

	{
		subroute::SubwooferRoutingState state = makeValidState();
		subroute::PrepareSpec spec = makePrepareSpec();
		spec.channelLayout = {"FL", "FR"};
		const subroute::ValidationResult result =
			subroute::validate(state, spec);
		harness.expectTrue(
			hasDiagnostic(
				result,
				subroute::DiagnosticCode::MissingDeviceChannel),
			"missing device channel should be diagnosed");
	}

	{
		subroute::SubwooferRoutingState state = makeValidState();
		state.headroom.mode = subroute::HeadroomMode::Manual;
		state.headroom.manualTrimDb = 1.0;
		const subroute::ValidationResult result = subroute::validate(state);
		harness.expectTrue(
			hasDiagnostic(
				result,
				subroute::DiagnosticCode::PositiveManualTrim),
			"positive manual trim should be diagnosed");
	}

	{
		subroute::SubwooferRoutingState state = makeValidState();
		state.paths.front().sourceMix.clear();
		const subroute::ValidationResult result = subroute::validate(state);
		harness.expectTrue(
			hasDiagnostic(result, subroute::DiagnosticCode::PathHasNoInputs),
			"empty source mix should be diagnosed");
	}

	{
		subroute::SubwooferRoutingState state = makeValidState();
		state.outputMatrix.push_back(state.outputMatrix.front());
		const subroute::ValidationResult result = subroute::validate(state);
		harness.expectTrue(
			hasDiagnostic(
				result,
				subroute::DiagnosticCode::DuplicateOutputTarget),
			"duplicate output target should be diagnosed");
	}

	{
		subroute::SubwooferRoutingState state = makeValidState();
		state.paths.front().id = "bad path id";
		const subroute::ValidationResult result = subroute::validate(state);
		harness.expectTrue(
			hasDiagnostic(result, subroute::DiagnosticCode::InvalidStableId),
			"invalid stable ID should be diagnosed");
	}
}

void testHighPassCoefficients()
{
	subroute::SubwooferRoutingState state = makeValidState();
	subroute::BiquadFilter filter;
	filter.type = subroute::BiquadType::HighPass;
	filter.frequencyHz = 1000.0;
	filter.q = 0.7071;
	state.paths.front().chain.push_back(subroute::BiquadStage{filter});

	const subroute::CompileResult result =
		subroute::compile(state, makePrepareSpec());

	harness.require(result.graph.has_value(), "high-pass state should compile");
	harness.require(
		!result.graph->paths().empty(),
		"high-pass graph should contain paths");

	const subroute::CompiledStage* stage = findStage(
		result.graph->paths().front(),
		subroute::CompiledStageKind::Biquad);
	harness.require(stage != nullptr, "high-pass path should contain a biquad");

	const double atCutoff =
		evaluateMagnitudeDb(stage->biquad, 1000.0, 48000.0);
	const double atTenTimesCutoff =
		evaluateMagnitudeDb(stage->biquad, 10000.0, 48000.0);

	harness.expectTrue(
		std::abs(atCutoff - (-3.01)) <= 0.1,
		"high-pass response at cutoff should be approximately -3.01 dB");
	harness.expectTrue(
		std::abs(atTenTimesCutoff) <= 0.2,
		"high-pass response at ten times cutoff should be approximately 0 dB");
}

void testDelayLowering()
{
	subroute::SubwooferRoutingState state = makeValidState();
	auto* delay =
		std::get_if<subroute::DelayStage>(&state.paths.front().chain[1]);
	harness.require(delay != nullptr, "test path should contain a delay stage");
	delay->milliseconds = 2.5;

	const subroute::CompileResult result =
		subroute::compile(state, makePrepareSpec());

	harness.require(result.graph.has_value(), "delayed state should compile");

	const subroute::CompiledStage* stage = findStage(
		result.graph->paths().front(),
		subroute::CompiledStageKind::Delay);
	harness.require(stage != nullptr, "compiled path should contain a delay");

	harness.expectEqual(
		stage->integerDelaySamples,
		static_cast<std::size_t>(120),
		"2.5 ms at 48 kHz should produce 120 integer samples");
	harness.expectTrue(
		std::abs(stage->fractionalDelaySamples) <= 1.0e-12,
		"2.5 ms at 48 kHz should have no fractional sample");
}

void testAutomaticHeadroomForTwoUnityTerms()
{
	subroute::PrepareSpec spec;
	spec.sampleRate = 48000.0;
	spec.maximumBlockSize = 1024;
	spec.channelLayout = {"A", "B"};

	const subroute::CompileResult result =
		subroute::compile(makeTwoTermHeadroomState(), spec);

	harness.require(result.headroom.has_value(), "headroom analysis should exist");
	harness.expectTrue(
		std::abs(result.headroom->appliedTrimDb - (-6.020599913279624))
			<= 0.05,
		"two unity terms should require approximately -6.02 dB trim");
	harness.expectTrue(
		std::abs(result.headroom->predictedPeakLinearBeforeTrim - 2.0)
			<= 1.0e-9,
		"two independent unity inputs should predict a peak of two");
}

void testManualHeadroomPassThrough()
{
	subroute::SubwooferRoutingState state = makeTwoTermHeadroomState();
	state.headroom.mode = subroute::HeadroomMode::Manual;
	state.headroom.manualTrimDb = -4.5;

	subroute::PrepareSpec spec;
	spec.sampleRate = 48000.0;
	spec.maximumBlockSize = 1024;
	spec.channelLayout = {"A", "B"};

	const subroute::CompileResult result = subroute::compile(state, spec);

	harness.require(result.headroom.has_value(), "manual headroom analysis should exist");
	harness.expectTrue(
		std::abs(result.headroom->appliedTrimDb - (-4.5)) <= 1.0e-12,
		"manual trim should pass through unchanged");
	harness.expectTrue(
		result.headroom->mode == subroute::HeadroomMode::Manual,
		"manual mode should be retained in the analysis");
}

}

void runSubwooferRoutingCompilerTests();

void runSubwooferRoutingCompilerTests()
{
	testValidStateCompiles();
	testRepresentativeDiagnostics();
	testHighPassCoefficients();
	testDelayLowering();
	testAutomaticHeadroomForTwoUnityTerms();
	testManualHeadroomPassThrough();
	harness.report();
}
