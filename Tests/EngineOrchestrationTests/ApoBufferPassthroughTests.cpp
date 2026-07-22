/*
	This file is part of EqualizerAPO-XT.

	Regression tests for unsupported APO connection formats. Some shared-mode
	clients hand the system-effect APO integer PCM in distinct input and output
	buffers. The DSP cannot interpret those samples, but bypass must preserve
	their bytes instead of reporting a silent output buffer.
*/

#include <array>
#include <cstdint>

#include "EqualizerAPO/ApoBufferPassthrough.h"
#include "Tests/TestHarness.h"

void runApoBufferPassthroughTests(test::Harness& harness)
{
	constexpr size_t frames = 3;
	constexpr size_t channels = 2;
	std::array<std::int32_t, frames * channels> input = {
		0x10203040, -0x1020304, 0x5060708, -0x1122334, 0x01020304, -1
	};
	std::array<std::int32_t, frames * channels> output = {};

	ApoBufferPassthroughPlan plan;
	plan.inputBytesPerFrame = channels * sizeof(std::int32_t);
	plan.outputBytesPerFrame = channels * sizeof(std::int32_t);
	plan.distinctBuffersAreCompatible = true;

	const ApoBufferState state = passthroughApoFrames(
		input.data(), output.data(), frames, false, plan);

	harness.expect(state == ApoBufferState::Valid,
		"compatible PCM in distinct APO buffers remains valid");
	harness.expect(output == input,
		"compatible PCM bytes are copied to the distinct output buffer");
}
