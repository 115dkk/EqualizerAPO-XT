/*
	This file is part of EqualizerAPO-XT.

	Real-time-safe byte passthrough for connection formats that the DSP engine
	cannot interpret. The caller validates the format and byte counts while the
	APO is locked; this function only copies or clears already-sized buffers.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

enum class ApoBufferState
{
	Valid,
	Silent
};

struct ApoBufferPassthroughPlan
{
	size_t inputBytesPerFrame = 0;
	size_t outputBytesPerFrame = 0;
	bool distinctBuffersAreCompatible = false;
};

struct ApoBufferLayout
{
	std::uint32_t samplesPerFrame = 0;
	std::uint32_t bytesPerSampleContainer = 0;
	std::uint32_t validBitsPerSample = 0;
	double framesPerSecond = 0.0;
	std::uint32_t channelMask = 0;
};

inline size_t checkedApoBytesPerFrame(const ApoBufferLayout& layout,
	size_t maxFrameCount) noexcept
{
	if (layout.samplesPerFrame == 0 || layout.bytesPerSampleContainer == 0)
		return 0;

	constexpr size_t maxSize = (std::numeric_limits<size_t>::max)();
	const size_t samplesPerFrame = layout.samplesPerFrame;
	const size_t bytesPerSample = layout.bytesPerSampleContainer;
	if (samplesPerFrame > maxSize / bytesPerSample)
		return 0;

	const size_t bytesPerFrame = samplesPerFrame * bytesPerSample;
	if (maxFrameCount > maxSize / bytesPerFrame)
		return 0;
	return bytesPerFrame;
}

inline ApoBufferPassthroughPlan makeApoBufferPassthroughPlan(
	const ApoBufferLayout& input, const ApoBufferLayout& output,
	bool sameEncoding, size_t maxFrameCount) noexcept
{
	ApoBufferPassthroughPlan plan;
	plan.inputBytesPerFrame = checkedApoBytesPerFrame(input, maxFrameCount);
	plan.outputBytesPerFrame = checkedApoBytesPerFrame(output, maxFrameCount);
	plan.distinctBuffersAreCompatible = sameEncoding
		&& plan.inputBytesPerFrame != 0
		&& plan.inputBytesPerFrame == plan.outputBytesPerFrame
		&& input.samplesPerFrame == output.samplesPerFrame
		&& input.bytesPerSampleContainer == output.bytesPerSampleContainer
		&& input.validBitsPerSample == output.validBitsPerSample
		&& input.framesPerSecond == output.framesPerSecond
		&& input.channelMask == output.channelMask;
	return plan;
}

inline bool checkedApoBufferSize(size_t frameCount, size_t bytesPerFrame,
	size_t& byteCount) noexcept
{
	if (bytesPerFrame == 0)
		return false;
	constexpr size_t maxSize = (std::numeric_limits<size_t>::max)();
	if (frameCount > maxSize / bytesPerFrame)
		return false;
	byteCount = frameCount * bytesPerFrame;
	return true;
}

inline ApoBufferState passthroughApoFrames(const void* input, void* output,
	size_t frameCount, bool inputIsSilent,
	const ApoBufferPassthroughPlan& plan) noexcept
{
	if (input == output)
		return inputIsSilent ? ApoBufferState::Silent : ApoBufferState::Valid;

	size_t outputByteCount = 0;
	if (!checkedApoBufferSize(frameCount, plan.outputBytesPerFrame, outputByteCount))
		return ApoBufferState::Silent;

	if (!inputIsSilent && plan.distinctBuffersAreCompatible)
	{
		std::memmove(output, input, outputByteCount);
		return ApoBufferState::Valid;
	}

	std::memset(output, 0, outputByteCount);
	return ApoBufferState::Silent;
}
