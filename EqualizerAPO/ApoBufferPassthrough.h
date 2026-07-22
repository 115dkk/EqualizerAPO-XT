/*
	This file is part of EqualizerAPO-XT.

	Real-time-safe byte passthrough for connection formats that the DSP engine
	cannot interpret. The caller validates the format and byte counts while the
	APO is locked; this function only copies or clears already-sized buffers.
*/

#pragma once

#include <cstddef>
#include <cstring>

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

inline ApoBufferState passthroughApoFrames(const void* input, void* output,
	size_t frameCount, bool inputIsSilent,
	const ApoBufferPassthroughPlan& plan) noexcept
{
	if (input == output)
		return inputIsSilent ? ApoBufferState::Silent : ApoBufferState::Valid;

	// Current behaviour: an unsupported format in distinct buffers is treated
	// as silence even when LockForProcess proved the byte layouts identical.
	// The regression test pins the required copy-through behaviour before this
	// branch is fixed.
	if (plan.outputBytesPerFrame != 0)
		std::memset(output, 0, frameCount * plan.outputBytesPerFrame);
	return ApoBufferState::Silent;
}
