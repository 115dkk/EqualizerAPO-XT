// SPDX-License-Identifier: MIT

#include "BassManagement/Compiler.h"
#include "BassManagement/Preset.h"
#include "BassManagement/Processor.h"
#include "Tests/TestHarness.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace
{

test::Harness harness("BassManagementProcessorTests");

bassmgmt::PrepareSpec makePrepareSpec(
	std::size_t maximumBlockSize,
	bool includeSideLeft = false)
{
	bassmgmt::PrepareSpec spec;
	spec.sampleRate = 48000.0;
	spec.maximumBlockSize = maximumBlockSize;
	spec.channelLayout = {"L", "R", "LFE", "RL", "RR"};

	if (includeSideLeft)
	{
		spec.channelLayout.push_back("SL");
	}

	return spec;
}

bassmgmt::CompileResult compilePreset(
	const bassmgmt::PrepareSpec& spec)
{
	const bassmgmt::PresetCreateResult preset =
		bassmgmt::createBuiltInPreset(
			bassmgmt::kIssue246FrontRear41PresetId);

	if (!preset.succeeded())
	{
		return bassmgmt::CompileResult{};
	}

	return bassmgmt::compile(*preset.state, spec);
}

bool approximatelyEqual(
	double actual,
	double expected,
	double tolerance)
{
	return std::abs(actual - expected) <= tolerance;
}

const bassmgmt::Path* findPath(
	const bassmgmt::BassManagementState& state,
	const std::string& id)
{
	const auto iterator = std::find_if(
		state.paths.begin(),
		state.paths.end(),
		[&id](const bassmgmt::Path& path)
		{
			return path.id == id;
		});

	return iterator == state.paths.end() ? nullptr : &*iterator;
}

const bassmgmt::OutputMatrixEntry* findOutput(
	const bassmgmt::BassManagementState& state,
	const std::string& targetChannelId)
{
	const auto iterator = std::find_if(
		state.outputMatrix.begin(),
		state.outputMatrix.end(),
		[&targetChannelId](
			const bassmgmt::OutputMatrixEntry& output)
		{
			return output.targetChannelId == targetChannelId;
		});

	return iterator == state.outputMatrix.end() ? nullptr : &*iterator;
}

std::vector<double> crossoverFrequencies(
	const bassmgmt::Path& path)
{
	std::vector<double> frequencies;

	for (const bassmgmt::PathStage& stage : path.chain)
	{
		if (const auto* biquad =
			std::get_if<bassmgmt::BiquadStage>(&stage))
		{
			frequencies.push_back(biquad->filter.frequencyHz);
		}
	}

	return frequencies;
}

double pathDelayMilliseconds(const bassmgmt::Path& path)
{
	for (const bassmgmt::PathStage& stage : path.chain)
	{
		if (const auto* delay =
			std::get_if<bassmgmt::DelayStage>(&stage))
		{
			return delay->milliseconds;
		}
	}

	return -1.0;
}

void testLfeOnlyImpulse()
{
	const bassmgmt::PrepareSpec spec = makePrepareSpec(64);
	bassmgmt::CompileResult compiled = compilePreset(spec);

	harness.expectTrue(
		compiled.succeeded(),
		"Issue #246 preset should compile for the canonical layout");

	if (!compiled.succeeded())
	{
		return;
	}

	bassmgmt::Processor processor;
	processor.prepare(spec, *compiled.graph);

	constexpr std::size_t frameCount = 64;
	std::vector<std::vector<double>> input(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));
	std::vector<std::vector<double>> output(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));

	input[2][0] = 1.0;

	std::vector<const double*> inputPlanes;
	std::vector<double*> outputPlanes;

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		inputPlanes.push_back(input[channel].data());
		outputPlanes.push_back(output[channel].data());
	}

	const bassmgmt::AudioBlock block(
		inputPlanes.data(),
		outputPlanes.data(),
		spec.channelLayout.size(),
		frameCount);
	processor.process(block);

	const double trim = compiled.graph->headroom().appliedTrimLinear;
	const double expectedFrontBeforeTrim = std::pow(10.0, 10.0 / 20.0);
	const double expectedLfeBeforeTrim =
		std::pow(10.0, (10.0 - 14.0) / 20.0);

	harness.expectTrue(
		trim > 0.0,
		"Compiled headroom trim should be positive linear gain");
	harness.expectTrue(
		approximatelyEqual(
			output[0][0] / trim,
			expectedFrontBeforeTrim,
			1.0e-11),
		"L should contain the +10 dB SourceLFE impulse before common trim");
	harness.expectTrue(
		approximatelyEqual(
			output[1][0] / trim,
			expectedFrontBeforeTrim,
			1.0e-11),
		"R should contain the +10 dB SourceLFE impulse before common trim");
	harness.expectTrue(
		approximatelyEqual(
			output[2][0] / trim,
			expectedLfeBeforeTrim,
			1.0e-11),
		"LFE should contain SourceLFE at +10 dB followed by -14 dB");
	harness.expectTrue(
		std::all_of(
			output[3].begin(),
			output[3].end(),
			[](double value)
			{
				return value == 0.0;
			}),
		"RL should remain exactly silent for an LFE-only impulse");
	harness.expectTrue(
		std::all_of(
			output[4].begin(),
			output[4].end(),
			[](double value)
			{
				return value == 0.0;
			}),
		"RR should remain exactly silent for an LFE-only impulse");
}

void testUntouchedChannelBitExactness()
{
	constexpr std::size_t frameCount = 257;
	const bassmgmt::PrepareSpec spec =
		makePrepareSpec(frameCount, true);
	bassmgmt::CompileResult compiled = compilePreset(spec);

	harness.expectTrue(
		compiled.succeeded(),
		"Preset should compile with an additional device channel");

	if (!compiled.succeeded())
	{
		return;
	}

	{
		bassmgmt::Processor processor;
		processor.prepare(spec, *compiled.graph);

		std::vector<std::vector<float>> input(
			spec.channelLayout.size(),
			std::vector<float>(frameCount, 0.0f));
		std::vector<std::vector<float>> output(
			spec.channelLayout.size(),
			std::vector<float>(frameCount, -99.0f));

		for (std::size_t frame = 0; frame < frameCount; ++frame)
		{
			const int ramp = static_cast<int>(frame % 31) - 15;
			input[5][frame] =
				static_cast<float>(ramp) * 0.03125f;
		}

		std::vector<const float*> inputPlanes;
		std::vector<float*> outputPlanes;

		for (std::size_t channel = 0;
			channel < spec.channelLayout.size();
			++channel)
		{
			inputPlanes.push_back(input[channel].data());
			outputPlanes.push_back(output[channel].data());
		}

		const bassmgmt::AudioBlock block(
			inputPlanes.data(),
			outputPlanes.data(),
			spec.channelLayout.size(),
			frameCount);
		processor.process(block);

		harness.expectTrue(
			std::memcmp(
				input[5].data(),
				output[5].data(),
				frameCount * sizeof(float)) == 0,
			"Untouched float channel should be bit-exact");
	}

	{
		bassmgmt::Processor processor;
		processor.prepare(spec, *compiled.graph);

		std::vector<std::vector<double>> input(
			spec.channelLayout.size(),
			std::vector<double>(frameCount, 0.0));
		std::vector<std::vector<double>> output(
			spec.channelLayout.size(),
			std::vector<double>(frameCount, -99.0));

		for (std::size_t frame = 0; frame < frameCount; ++frame)
		{
			const int ramp = static_cast<int>(frame % 43) - 21;
			input[5][frame] =
				static_cast<double>(ramp) * 0.015625;
		}

		std::vector<const double*> inputPlanes;
		std::vector<double*> outputPlanes;

		for (std::size_t channel = 0;
			channel < spec.channelLayout.size();
			++channel)
		{
			inputPlanes.push_back(input[channel].data());
			outputPlanes.push_back(output[channel].data());
		}

		const bassmgmt::AudioBlock block(
			inputPlanes.data(),
			outputPlanes.data(),
			spec.channelLayout.size(),
			frameCount);
		processor.process(block);

		harness.expectTrue(
			std::memcmp(
				input[5].data(),
				output[5].data(),
				frameCount * sizeof(double)) == 0,
			"Untouched double channel should be bit-exact");
	}
}

void testBlockSplitContinuity()
{
	constexpr std::size_t frameCount = 4096;
	constexpr std::size_t splitSize = 512;
	const bassmgmt::PrepareSpec spec = makePrepareSpec(frameCount);
	bassmgmt::CompileResult compiled = compilePreset(spec);

	harness.expectTrue(
		compiled.succeeded(),
		"Preset should compile for block continuity testing");

	if (!compiled.succeeded())
	{
		return;
	}

	std::vector<std::vector<double>> input(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));
	std::vector<std::vector<double>> wholeOutput(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));
	std::vector<std::vector<double>> splitOutput(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		for (std::size_t frame = 0; frame < frameCount; ++frame)
		{
			input[channel][frame] =
				0.1 * std::sin(
					0.011 * static_cast<double>(
						frame + channel * 17)) +
				0.03 * std::cos(
					0.007 * static_cast<double>(
						frame * (channel + 1)));
		}
	}

	bassmgmt::Processor wholeProcessor;
	wholeProcessor.prepare(spec, *compiled.graph);

	std::vector<const double*> wholeInputPlanes;
	std::vector<double*> wholeOutputPlanes;

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		wholeInputPlanes.push_back(input[channel].data());
		wholeOutputPlanes.push_back(wholeOutput[channel].data());
	}

	const bassmgmt::AudioBlock wholeBlock(
		wholeInputPlanes.data(),
		wholeOutputPlanes.data(),
		spec.channelLayout.size(),
		frameCount);
	wholeProcessor.process(wholeBlock);

	bassmgmt::Processor splitProcessor;
	splitProcessor.prepare(spec, *compiled.graph);

	for (std::size_t offset = 0;
		offset < frameCount;
		offset += splitSize)
	{
		std::vector<const double*> splitInputPlanes;
		std::vector<double*> splitOutputPlanes;

		for (std::size_t channel = 0;
			channel < spec.channelLayout.size();
			++channel)
		{
			splitInputPlanes.push_back(
				input[channel].data() + offset);
			splitOutputPlanes.push_back(
				splitOutput[channel].data() + offset);
		}

		const bassmgmt::AudioBlock splitBlock(
			splitInputPlanes.data(),
			splitOutputPlanes.data(),
			spec.channelLayout.size(),
			splitSize);
		splitProcessor.process(splitBlock);
	}

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		harness.expectTrue(
			std::memcmp(
				wholeOutput[channel].data(),
				splitOutput[channel].data(),
				frameCount * sizeof(double)) == 0,
			"Split and whole-block double outputs should be identical");
	}
}

void testFrontBassPolarity()
{
	constexpr std::size_t frameCount = 256;
	const bassmgmt::PrepareSpec spec = makePrepareSpec(frameCount);
	bassmgmt::CompileResult compiled = compilePreset(spec);

	harness.expectTrue(
		compiled.succeeded(),
		"Preset should compile for polarity testing");

	if (!compiled.succeeded())
	{
		return;
	}

	bassmgmt::Processor processor;
	processor.prepare(spec, *compiled.graph);

	std::vector<std::vector<double>> input(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));
	std::vector<std::vector<double>> output(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));

	input[0][0] = 1.0;

	std::vector<const double*> inputPlanes;
	std::vector<double*> outputPlanes;

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		inputPlanes.push_back(input[channel].data());
		outputPlanes.push_back(output[channel].data());
	}

	const bassmgmt::AudioBlock block(
		inputPlanes.data(),
		outputPlanes.data(),
		spec.channelLayout.size(),
		frameCount);
	processor.process(block);

	const auto firstNonZero = std::find_if(
		output[0].begin(),
		output[0].end(),
		[](double value)
		{
			return value != 0.0;
		});

	harness.expectTrue(
		firstNonZero != output[0].end(),
		"L-only impulse should produce a FrontBass output");
	harness.expectTrue(
		firstNonZero != output[0].end() && *firstNonZero < 0.0,
		"FrontBass's negative L source mix should produce negative low-band polarity");
}

void testInPlaceAliasing()
{
	constexpr std::size_t frameCount = 1024;
	const bassmgmt::PrepareSpec spec = makePrepareSpec(frameCount);
	bassmgmt::CompileResult compiled = compilePreset(spec);

	harness.expectTrue(
		compiled.succeeded(),
		"Preset should compile for in-place testing");

	if (!compiled.succeeded())
	{
		return;
	}

	std::vector<std::vector<double>> input(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		for (std::size_t frame = 0; frame < frameCount; ++frame)
		{
			input[channel][frame] =
				0.2 * std::sin(
					0.019 * static_cast<double>(
						frame + channel * 23));
		}
	}

	std::vector<std::vector<double>> referenceOutput(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));
	std::vector<std::vector<double>> inPlaceData = input;

	{
		bassmgmt::Processor processor;
		processor.prepare(spec, *compiled.graph);

		std::vector<const double*> inputPlanes;
		std::vector<double*> outputPlanes;

		for (std::size_t channel = 0;
			channel < spec.channelLayout.size();
			++channel)
		{
			inputPlanes.push_back(input[channel].data());
			outputPlanes.push_back(referenceOutput[channel].data());
		}

		const bassmgmt::AudioBlock block(
			inputPlanes.data(),
			outputPlanes.data(),
			spec.channelLayout.size(),
			frameCount);
		processor.process(block);
	}

	{
		bassmgmt::Processor processor;
		processor.prepare(spec, *compiled.graph);

		std::vector<const double*> inputPlanes;
		std::vector<double*> outputPlanes;

		for (std::size_t channel = 0;
			channel < spec.channelLayout.size();
			++channel)
		{
			inputPlanes.push_back(inPlaceData[channel].data());
			outputPlanes.push_back(inPlaceData[channel].data());
		}

		const bassmgmt::AudioBlock block(
			inputPlanes.data(),
			outputPlanes.data(),
			spec.channelLayout.size(),
			frameCount);
		processor.process(block);
	}

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		harness.expectTrue(
			std::memcmp(
				referenceOutput[channel].data(),
				inPlaceData[channel].data(),
				frameCount * sizeof(double)) == 0,
			"In-place output should equal non-aliased output");
	}
}

void testResetReproducesImpulse()
{
	constexpr std::size_t frameCount = 512;
	const bassmgmt::PrepareSpec spec = makePrepareSpec(frameCount);
	bassmgmt::CompileResult compiled = compilePreset(spec);

	harness.expectTrue(
		compiled.succeeded(),
		"Preset should compile for reset testing");

	if (!compiled.succeeded())
	{
		return;
	}

	bassmgmt::Processor processor;
	processor.prepare(spec, *compiled.graph);

	std::vector<std::vector<double>> input(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));
	std::vector<std::vector<double>> firstOutput(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));
	std::vector<std::vector<double>> secondOutput(
		spec.channelLayout.size(),
		std::vector<double>(frameCount, 0.0));

	input[0][0] = 1.0;
	input[2][0] = 0.5;

	std::vector<const double*> inputPlanes;
	std::vector<double*> firstOutputPlanes;

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		inputPlanes.push_back(input[channel].data());
		firstOutputPlanes.push_back(firstOutput[channel].data());
	}

	const bassmgmt::AudioBlock firstBlock(
		inputPlanes.data(),
		firstOutputPlanes.data(),
		spec.channelLayout.size(),
		frameCount);
	processor.process(firstBlock);

	processor.reset();

	std::vector<double*> secondOutputPlanes;

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		secondOutputPlanes.push_back(secondOutput[channel].data());
	}

	const bassmgmt::AudioBlock secondBlock(
		inputPlanes.data(),
		secondOutputPlanes.data(),
		spec.channelLayout.size(),
		frameCount);
	processor.process(secondBlock);

	for (std::size_t channel = 0;
		channel < spec.channelLayout.size();
		++channel)
	{
		harness.expectTrue(
			std::memcmp(
				firstOutput[channel].data(),
				secondOutput[channel].data(),
				frameCount * sizeof(double)) == 0,
			"Reset should restore impulse processing to the initial state");
	}
}

void testPresetStructure()
{
	const bassmgmt::PresetCreateResult preset =
		bassmgmt::createBuiltInPreset(
			bassmgmt::kIssue246FrontRear41PresetId);

	harness.expectTrue(
		preset.succeeded(),
		"Issue #246 built-in preset creation should succeed");

	if (!preset.succeeded())
	{
		return;
	}

	const bassmgmt::BassManagementState& state = *preset.state;
	const bassmgmt::ValidationResult validation =
		bassmgmt::validate(state);

	harness.expectTrue(
		!validation.hasErrors(),
		"Issue #246 preset should have no validation errors");
	harness.expectEqual(
		state.paths.size(),
		static_cast<std::size_t>(7),
		"Issue #246 preset should contain seven paths");
	harness.expectEqual(
		state.outputMatrix.size(),
		static_cast<std::size_t>(5),
		"Issue #246 preset should contain five matrix outputs");

	const bassmgmt::Path* frontL = findPath(state, "FrontL");
	const bassmgmt::Path* frontBass = findPath(state, "FrontBass");
	const bassmgmt::Path* rearL = findPath(state, "RearL");
	const bassmgmt::Path* rearBass = findPath(state, "RearBass");
	const bassmgmt::Path* sourceLfe = findPath(state, "SourceLFE");

	harness.expectTrue(frontL != nullptr, "FrontL path should exist");
	harness.expectTrue(
		frontBass != nullptr,
		"FrontBass path should exist");
	harness.expectTrue(rearL != nullptr, "RearL path should exist");
	harness.expectTrue(
		rearBass != nullptr,
		"RearBass path should exist");
	harness.expectTrue(
		sourceLfe != nullptr,
		"SourceLFE path should exist");

	if (frontL != nullptr)
	{
		const std::vector<double> frequencies =
			crossoverFrequencies(*frontL);

		harness.expectEqual(
			frequencies.size(),
			static_cast<std::size_t>(1),
			"FrontL should have one crossover section");
		harness.expectTrue(
			frequencies.size() == 1 && frequencies[0] == 80.0,
			"FrontL crossover should be 80 Hz");
		harness.expectTrue(
			pathDelayMilliseconds(*frontL) == 2.5,
			"FrontL delay should be 2.5 ms");
	}

	if (frontBass != nullptr)
	{
		const std::vector<double> frequencies =
			crossoverFrequencies(*frontBass);

		harness.expectEqual(
			frequencies.size(),
			static_cast<std::size_t>(2),
			"FrontBass should have two crossover sections");
		harness.expectTrue(
			frequencies.size() == 2 &&
				frequencies[0] == 80.0 &&
				frequencies[1] == 80.0,
			"FrontBass crossover sections should both be 80 Hz");
	}

	if (rearL != nullptr)
	{
		const std::vector<double> frequencies =
			crossoverFrequencies(*rearL);

		harness.expectTrue(
			frequencies.size() == 1 && frequencies[0] == 100.0,
			"RearL crossover should be 100 Hz");
		harness.expectTrue(
			pathDelayMilliseconds(*rearL) == 2.0,
			"RearL delay should be 2.0 ms");
	}

	if (rearBass != nullptr)
	{
		const std::vector<double> frequencies =
			crossoverFrequencies(*rearBass);

		harness.expectTrue(
			frequencies.size() == 1 && frequencies[0] == 60.0,
			"RearBass crossover should be 60 Hz");
	}

	if (sourceLfe != nullptr)
	{
		harness.expectTrue(
			sourceLfe->preGainDb == 10.0,
			"SourceLFE pre-gain should be +10 dB");
	}

	const bassmgmt::OutputMatrixEntry* lfe =
		findOutput(state, "LFE");

	harness.expectTrue(
		lfe != nullptr,
		"LFE matrix output should exist");

	if (lfe != nullptr)
	{
		const auto sourceLfeTerm = std::find_if(
			lfe->terms.begin(),
			lfe->terms.end(),
			[](const bassmgmt::OutputMatrixTerm& term)
			{
				return term.sourcePathId == "SourceLFE";
			});

		harness.expectTrue(
			sourceLfeTerm != lfe->terms.end(),
			"LFE output should reference SourceLFE");
		harness.expectTrue(
			sourceLfeTerm != lfe->terms.end() &&
				sourceLfeTerm->gainDb == -14.0,
			"LFE SourceLFE matrix gain should be -14 dB");
	}
}

}

void runBassManagementProcessorTests()
{
	testLfeOnlyImpulse();
	testUntouchedChannelBitExactness();
	testBlockSplitContinuity();
	testFrontBassPolarity();
	testInPlaceAliasing();
	testResetReproducesImpulse();
	testPresetStructure();
	harness.report();
}
