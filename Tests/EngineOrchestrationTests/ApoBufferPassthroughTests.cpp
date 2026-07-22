/*
	This file is part of EqualizerAPO-XT.

	Regression tests for unsupported APO connection formats. Some shared-mode
	clients hand the system-effect APO integer PCM in distinct input and output
	buffers. The DSP cannot interpret those samples, but bypass must preserve
	their bytes instead of reporting a silent output buffer.
*/

#include <array>
#include <cstdint>
#include <limits>

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

	const ApoBufferLayout pcm32Stereo = {
		static_cast<std::uint32_t>(channels),
		static_cast<std::uint32_t>(sizeof(std::int32_t)),
		32, 48000.0, 3
	};
	const ApoBufferPassthroughPlan plan = makeApoBufferPassthroughPlan(
		pcm32Stereo, pcm32Stereo, true, frames);

	const ApoBufferState state = passthroughApoFrames(
		input.data(), output.data(), frames, false, plan);

	harness.expect(state == ApoBufferState::Valid,
		"compatible PCM in distinct APO buffers remains valid");
	harness.expect(output == input,
		"compatible PCM bytes are copied to the distinct output buffer");

	output.fill(-1);
	const ApoBufferState silentState = passthroughApoFrames(
		input.data(), output.data(), frames, true, plan);
	harness.expect(silentState == ApoBufferState::Silent,
		"silent opaque input remains marked silent");
	harness.expect(output == decltype(output){},
		"silent opaque input clears the distinct output buffer");

	ApoBufferLayout differentOutput = pcm32Stereo;
	differentOutput.validBitsPerSample = 24;
	const ApoBufferPassthroughPlan incompatiblePlan = makeApoBufferPassthroughPlan(
		pcm32Stereo, differentOutput, true, frames);
	harness.expect(!incompatiblePlan.distinctBuffersAreCompatible,
		"different PCM valid-bit layouts are not byte-passthrough compatible");

	output.fill(-1);
	const ApoBufferState incompatibleState = passthroughApoFrames(
		input.data(), output.data(), frames, false, incompatiblePlan);
	harness.expect(incompatibleState == ApoBufferState::Silent,
		"incompatible distinct APO buffers fail closed");
	harness.expect(output == decltype(output){},
		"incompatible distinct APO output is cleared");

	const ApoBufferPassthroughPlan differentEncodingPlan = makeApoBufferPassthroughPlan(
		pcm32Stereo, pcm32Stereo, false, frames);
	harness.expect(!differentEncodingPlan.distinctBuffersAreCompatible,
		"equal-sized samples with different encoding GUIDs are not compatible");

	ApoBufferLayout oversizedLayout = pcm32Stereo;
	oversizedLayout.samplesPerFrame = (std::numeric_limits<std::uint32_t>::max)();
	oversizedLayout.bytesPerSampleContainer = (std::numeric_limits<std::uint32_t>::max)();
	const ApoBufferPassthroughPlan oversizedPlan = makeApoBufferPassthroughPlan(
		oversizedLayout, oversizedLayout, true, frames);
	harness.expectEqual(oversizedPlan.inputBytesPerFrame, size_t{0},
		"overflowing input buffer size is rejected while locking");
	harness.expectEqual(oversizedPlan.outputBytesPerFrame, size_t{0},
		"overflowing output buffer size is rejected while locking");
	harness.expect(!oversizedPlan.distinctBuffersAreCompatible,
		"overflowing opaque layouts cannot enable byte passthrough");
}
