/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Pins the APO DLL's pure decisions (EqualizerAPO/ApoFormat.h) - format
	detection, the container-size rule, the silence verdict and the
	channel-mask fallback. These used to be locked inside the COM class's
	anonymous namespace, so the code that runs directly on users' audio
	endpoints had zero direct tests (audit #275 A8/TD-28).
*/

#include <algorithm>
#include <cstddef>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "EqualizerAPO/ApoFormat.h"
#include "Tests/TestHarness.h"

namespace
{

UNCOMPRESSEDAUDIOFORMAT makeFormat(const GUID& formatType, unsigned containerBytes, unsigned validBits)
{
	UNCOMPRESSEDAUDIOFORMAT format = {};
	format.guidFormatType = formatType;
	format.dwSamplesPerFrame = 2;
	format.dwBytesPerSampleContainer = containerBytes;
	format.dwValidBitsPerSample = validBits;
	format.fFramesPerSecond = 48000.0f;
	format.dwChannelMask = 0x3;
	return format;
}

void testFormatDetection(test::Harness& harness)
{
	harness.expect(apo::detectSampleFormat(makeFormat(apo::kIeeeFloatSubtype, 4, 32))
		== apo::SampleFormat::Float32, "a 4-byte IEEE float container is Float32");
	harness.expect(apo::detectSampleFormat(makeFormat(apo::kIeeeFloatSubtype, 8, 64))
		== apo::SampleFormat::Float64, "an 8-byte IEEE float container is Float64");
	// The rule that keeps virtual devices alive: only the container size
	// matters, because CABLE-style adapters report non-canonical valid-bit
	// counts on a plain float container.
	harness.expect(apo::detectSampleFormat(makeFormat(apo::kIeeeFloatSubtype, 4, 24))
		== apo::SampleFormat::Float32,
		"non-canonical valid bits on a float container still detect as Float32");
	harness.expect(apo::detectSampleFormat(makeFormat(apo::kIeeeFloatSubtype, 3, 24))
		== apo::SampleFormat::Unsupported, "an odd container size is unsupported");
	constexpr GUID pcmSubtype =
		{ 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
	harness.expect(apo::detectSampleFormat(makeFormat(pcmSubtype, 4, 32))
		== apo::SampleFormat::Unsupported, "an integer PCM format is unsupported");

	harness.expectEqual(apo::bytesPerSample(apo::SampleFormat::Float32), sizeof(float),
		"Float32 samples are float-sized");
	harness.expectEqual(apo::bytesPerSample(apo::SampleFormat::Float64), sizeof(double),
		"Float64 samples are double-sized");
	harness.expectEqual(apo::bytesPerSample(apo::SampleFormat::Unsupported), size_t(0),
		"an unsupported format has no sample size");
}

void testSilenceVerdict(test::Harness& harness)
{
	std::vector<double> block(256, 0.0);
	harness.expectTrue(apo::isBlockSilent(block.data(), block.size()),
		"an all-zero block is silent");
	block[100] = 5e-11;
	harness.expectTrue(apo::isBlockSilent(block.data(), block.size()),
		"sub-threshold residue still counts as silent");
	block[100] = 1e-9;
	harness.expectFalse(apo::isBlockSilent(block.data(), block.size()),
		"audible residue above the threshold is not silent");

	std::vector<float> floatBlock(64, 0.0f);
	harness.expectTrue(apo::isBlockSilent(floatBlock.data(), floatBlock.size()),
		"the float instantiation renders the same verdict");
	floatBlock[63] = -1e-3f;
	harness.expectFalse(apo::isBlockSilent(floatBlock.data(), floatBlock.size()),
		"a negative sample's magnitude counts");
}

void testChannelMaskFallback(test::Harness& harness)
{
	// Render prefers the output mask, capture the input mask.
	harness.expectEqual(apo::resolveChannelMask(false, 0x3F, 6, 0x3, 2), 0x3u,
		"render takes the output connection's mask");
	harness.expectEqual(apo::resolveChannelMask(true, 0x3F, 6, 0x3, 2), 0x3Fu,
		"capture takes the input connection's mask");
	// The fallback: a zero preferred mask borrows the opposite side's, but
	// only when the channel counts agree - some drivers fill only one side in.
	harness.expectEqual(apo::resolveChannelMask(false, 0x3F, 2, 0, 2), 0x3Fu,
		"a zero render mask borrows the input mask when the counts agree");
	harness.expectEqual(apo::resolveChannelMask(true, 0, 2, 0x3, 2), 0x3u,
		"a zero capture mask borrows the output mask when the counts agree");
	harness.expectEqual(apo::resolveChannelMask(false, 0x3F, 6, 0, 2), 0u,
		"no borrowing across different channel counts");
}

// Audit #348 F20: every branch of APOProcess, one fact changed at a time
// from a block that takes the silent fast path.
void testBlockActionTable(test::Harness& harness)
{
	using apo::BlockAction;
	using apo::SampleFormat;

	apo::BlockFacts quiet;
	quiet.inputFlags = BUFFER_SILENT;
	quiet.allowSilentBufferModification = false;
	quiet.hasChildApo = false;
	quiet.engineHasStatefulOrTailFilters = false;
	quiet.inputFormat = SampleFormat::Float32;
	quiet.outputFormat = SampleFormat::Float32;
	quiet.inPlace = true;
	harness.expect(apo::chooseBlockAction(quiet) == BlockAction::SilentFastPath,
		"silent input that nothing could make audible skips the engine");

	apo::BlockFacts facts = quiet;
	facts.inputFlags = BUFFER_INVALID;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::Ignore, "an invalid buffer is left alone");

	facts = quiet;
	facts.allowSilentBufferModification = true;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::ProcessFloat32,
		"a host that lets a silent buffer turn audible gets the engine's output");
	facts = quiet;
	facts.hasChildApo = true;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::ProcessFloat32,
		"a child APO could synthesize audio, so the engine runs");
	facts = quiet;
	facts.engineHasStatefulOrTailFilters = true;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::ProcessFloat32,
		"a filter with a tail can make silence audible, so the engine runs");

	facts = quiet;
	facts.inputFormat = SampleFormat::Unsupported;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::SilentFastPath,
		"the fast path only zeroes the output, so it needs only the output's sample size");
	facts = quiet;
	facts.outputFormat = SampleFormat::Unsupported;
	facts.inputFormat = SampleFormat::Unsupported;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::PassThroughInPlace,
		"with no known output size the fast path is not taken, and an in-place block passes through");

	facts = quiet;
	facts.inputFlags = BUFFER_VALID;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::ProcessFloat32, "valid float32 blocks are processed");
	facts.inputFormat = SampleFormat::Float64;
	facts.outputFormat = SampleFormat::Float64;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::ProcessFloat64, "valid float64 blocks are processed");
	facts.outputFormat = SampleFormat::Float32;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::PassThroughInPlace,
		"mismatched formats are never reinterpreted; an in-place block passes through");
	facts.inPlace = false;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::SilenceDistinctBuffers,
		"with distinct buffers and an unknown input size the output is zeroed instead");
	facts.inputFormat = SampleFormat::Unsupported;
	facts.outputFormat = SampleFormat::Unsupported;
	facts.inPlace = true;
	harness.expect(apo::chooseBlockAction(facts) == BlockAction::PassThroughInPlace,
		"an unsupported in-place block is passed through, not muted");
}

struct FakeBlockEngine
{
	unsigned sampleCount = 0;
	int processCalls = 0;
	bool inputWasOutput = false;
	bool inputWasZero = false;
	bool doubleInput = false;
	float generatedValue = 0.0f;

	void process(float* output, float* input, unsigned)
	{
		processCalls++;
		inputWasOutput = output == input;
		inputWasZero = std::all_of(input, input + sampleCount,
			[](float sample) {return sample == 0.0f;});
		if (doubleInput)
		{
			for (unsigned i = 0; i < sampleCount; ++i)
				output[i] = input[i] * 2.0f;
		}
		else
		{
			std::fill_n(output, sampleCount, generatedValue);
		}
	}
};

// Audit #422 E4: drive the complete block plan through an engine-like object.
// This pins the zero-input rule, child ordering, post-process silence policy and
// the non-processing actions without constructing an APO COM object.
void testBlockPlanExecution(test::Harness& harness)
{
	using apo::BlockAction;
	using apo::OutputFlagRule;
	using apo::SampleFormat;

	apo::BlockFacts facts;
	facts.inputFlags = BUFFER_SILENT;
	facts.inputFormat = SampleFormat::Float32;
	facts.outputFormat = SampleFormat::Float32;
	facts.inPlace = false;
	facts.engineHasStatefulOrTailFilters = true;
	facts.allowSilentBufferModification = true;

	constexpr unsigned frames = 4;
	constexpr unsigned channels = 2;
	float input[frames * channels];
	float output[frames * channels];
	std::fill_n(input, frames * channels, 3.0f);
	std::fill_n(output, frames * channels, -1.0f);
	FakeBlockEngine engine;
	engine.sampleCount = frames * channels;
	engine.generatedValue = 0.25f;
	int childCalls = 0;
	auto child = [&] {childCalls++;};

	const apo::BlockPlan audiblePlan = apo::makeBlockPlan(facts, frames);
	harness.expect(audiblePlan.action == BlockAction::ProcessFloat32,
		"a stateful engine processes silent float input");
	harness.expect(audiblePlan.zeroInputFirst,
		"the plan zeroes nominally silent input before processing");
	harness.expect(audiblePlan.outputFlagRule == OutputFlagRule::InspectProcessedOutput,
		"a modifiable silent buffer is inspected after processing");
	const apo::BlockResult audible = apo::processBlock(audiblePlan,
		input, output, channels, channels, engine, child);
	harness.expect(engine.inputWasZero, "the fake engine receives zeros for silent input");
	harness.expectEqual(childCalls, 0, "a plan without a child does not call one");
	harness.expect(audible.outputFlags == BUFFER_VALID,
		"audible output synthesized from silence is marked valid");

	facts.allowSilentBufferModification = false;
	const apo::BlockPlan forcedSilentPlan = apo::makeBlockPlan(facts, frames);
	std::fill_n(input, frames * channels, 3.0f);
	std::fill_n(output, frames * channels, -1.0f);
	const apo::BlockResult forcedSilent = apo::processBlock(forcedSilentPlan,
		input, output, channels, channels, engine, child);
	harness.expect(forcedSilent.outputFlags == BUFFER_SILENT,
		"an unmodifiable silent input remains marked silent");
	harness.expect(std::all_of(output, output + frames * channels,
		[](float sample) {return sample == 0.0f;}),
		"an unmodifiable silent input discards synthesized output");

	facts.inputFlags = BUFFER_VALID;
	facts.hasChildApo = true;
	facts.allowSilentBufferModification = false;
	const apo::BlockPlan childPlan = apo::makeBlockPlan(facts, frames);
	std::fill_n(output, frames * channels, -1.0f);
	engine.doubleInput = true;
	auto producingChild = [&] {
		childCalls++;
		std::fill_n(output, frames * channels, 0.5f);
	};
	const apo::BlockResult childResult = apo::processBlock(childPlan,
		input, output, channels, channels, engine, producingChild);
	harness.expect(engine.inputWasOutput,
		"after a child APO the engine processes the child output in place");
	harness.expectEqual(childCalls, 1, "the child runs once before the engine");
	harness.expect(std::all_of(output, output + frames * channels,
		[](float sample) {return sample == 1.0f;}),
		"the fake engine receives and transforms the child output");
	harness.expect(childResult.outputFlags == BUFFER_VALID,
		"a valid processed block remains valid");

	facts.inputFlags = BUFFER_SILENT;
	facts.hasChildApo = false;
	facts.engineHasStatefulOrTailFilters = false;
	const apo::BlockPlan fastPlan = apo::makeBlockPlan(facts, frames);
	std::fill_n(output, frames * channels, -1.0f);
	const int callsBeforeFastPath = engine.processCalls;
	const apo::BlockResult fast = apo::processBlock(fastPlan,
		input, output, channels, channels, engine, child);
	harness.expectEqual(engine.processCalls, callsBeforeFastPath,
		"the silent fast path skips the engine");
	harness.expect(fast.outputFlags == BUFFER_SILENT
		&& std::all_of(output, output + frames * channels,
			[](float sample) {return sample == 0.0f;}),
		"the silent fast path clears and marks the output");

	facts.inputFlags = BUFFER_VALID;
	facts.inputFormat = SampleFormat::Float64;
	facts.outputFormat = SampleFormat::Float32;
	facts.inPlace = true;
	const apo::BlockPlan passPlan = apo::makeBlockPlan(facts, frames);
	std::fill_n(output, frames * channels, 7.0f);
	const apo::BlockResult pass = apo::processBlock(passPlan,
		output, output, channels, channels, engine, child);
	harness.expect(pass.outputFlags == BUFFER_VALID
		&& std::all_of(output, output + frames * channels,
			[](float sample) {return sample == 7.0f;}),
		"an in-place format mismatch preserves samples and input validity");
}

} // namespace

// Called from the suite's main() in EngineOrchestrationTests.cpp.
void runApoFormatTests(test::Harness& harness)
{
	testFormatDetection(harness);
	testSilenceVerdict(harness);
	testChannelMaskFallback(harness);
	testBlockActionTable(harness);
	testBlockPlanExecution(harness);
}
