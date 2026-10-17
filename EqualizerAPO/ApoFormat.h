/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioenginebaseapo.h>

#include "audio/SampleFormat.h"

// The pure decisions of the APO DLL, pulled out of the COM class (audit #275
// A8/TD-28). The COM aggregation and the audiodg interaction genuinely cannot
// be tested automatically, but format detection, the silence verdict and the
// channel-mask fallback are plain functions - and they are the parts that are
// hardest to find when they go quietly wrong on a user's endpoint. They live
// in this ATL-free header so EngineOrchestrationTests can pin them.
namespace apo
{

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, spelled by value: including ks.h/ksmedia.h
// here would inject the KS GUID macro system into every consumer, where it
// breaks cguid.h (GUID_NULL becomes an __uuidof alias) in TUs that include
// COM headers afterwards.
inline constexpr GUID kIeeeFloatSubtype =
	{ 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

using SampleFormat = audio::SampleFormat;

inline SampleFormat detectSampleFormat(const UNCOMPRESSEDAUDIOFORMAT& f)
{
	// Windows audio engine normally hands system-effect APOs IEEE_FLOAT samples
	// even when the endpoint runs an integer format underneath. The rule
	// itself is audio::sampleFormatFor, which the Editor's AudioFormatProbe
	// also uses; a format it rejects falls through to passthrough.
	return audio::sampleFormatFor(IsEqualGUID(f.guidFormatType, kIeeeFloatSubtype) != 0,
		f.dwBytesPerSampleContainer);
}

inline size_t bytesPerSample(SampleFormat format)
{
	switch (format)
	{
	case SampleFormat::Float32: return sizeof(float);
	case SampleFormat::Float64: return sizeof(double);
	default: return 0;
	}
}

// The silence verdict processBlock() renders on a nominally-silent input
// buffer after the engine ran: BUFFER_SILENT seems to be important for some
// sound card drivers, so only report audible output if there really is audio
// above this threshold.
template<typename SampleT>
inline bool isBlockSilent(const SampleT* samples, size_t sampleCount)
{
	const SampleT threshold = static_cast<SampleT>(1e-10);
	for (size_t i = 0; i < sampleCount; i++)
	{
		if (std::abs(samples[i]) > threshold)
			return false;
	}
	return true;
}

// What APOProcess does with one block. The value and executor live together in
// this ATL-free header so tests cover the entire decision, including the
// zero-input and output-flag rules that used to remain in the COM class.
enum class BlockAction
{
	// Flags other than BUFFER_VALID and BUFFER_SILENT: leave the connection alone.
	Ignore,
	// Silent input that nothing could make audible (no stateful or tail-bearing
	// filter, no child APO, and the host does not allow a silent buffer to turn
	// audible): zero the output and report it silent, without the engine.
	SilentFastPath,
	ProcessFloat32,
	ProcessFloat64,
	// An unsupported or mismatched format on an in-place connection
	// (APO_FLAG_INPLACE): the samples already sit in the output buffer, so pass
	// them through with the input's flag. Reporting silence here would mute the
	// device the moment the APO is installed.
	PassThroughInPlace,
	// The same with distinct buffers, which a conformant host never hands an
	// in-place APO: the input's container size is unknown, so copying could
	// truncate or overrun; zero the output and report silence instead.
	SilenceDistinctBuffers
};

// What the choice depends on, read off the connections and the instance.
struct BlockFacts
{
	APO_BUFFER_FLAGS inputFlags = BUFFER_INVALID;
	bool allowSilentBufferModification = false;
	bool hasChildApo = false;
	bool engineHasStatefulOrTailFilters = true;
	SampleFormat inputFormat = SampleFormat::Unsupported;
	SampleFormat outputFormat = SampleFormat::Unsupported;
	bool inPlace = false;
};

enum class OutputFlagRule
{
	Preserve,
	Silent,
	Valid,
	InspectProcessedOutput,
	CopyInput
};

struct BlockPlan
{
	BlockAction action = BlockAction::Ignore;
	unsigned frameCount = 0;
	size_t outputBytesPerSample = 0;
	bool zeroInputFirst = false;
	bool processChildFirst = false;
	OutputFlagRule outputFlagRule = OutputFlagRule::Preserve;
	APO_BUFFER_FLAGS inputFlags = BUFFER_INVALID;
};

inline BlockAction chooseBlockAction(const BlockFacts& facts)
{
	if (facts.inputFlags != BUFFER_VALID && facts.inputFlags != BUFFER_SILENT)
		return BlockAction::Ignore;

	// The fast path only needs to zero the output, so any output format with a
	// known sample size will do; an unknown one goes through the normal
	// branches so it is never mishandled silently.
	if (facts.inputFlags == BUFFER_SILENT && !facts.allowSilentBufferModification && !facts.hasChildApo
		&& !facts.engineHasStatefulOrTailFilters && bytesPerSample(facts.outputFormat) > 0)
	{
		return BlockAction::SilentFastPath;
	}

	// The APO is registered with APO_FLAG_BITSPERSAMPLE_MUST_MATCH, so both
	// sides should agree; the engine runs only when they do and the format is
	// one it can read, so integer samples are never reinterpreted as float.
	if (facts.inputFormat == facts.outputFormat && facts.inputFormat == SampleFormat::Float64)
		return BlockAction::ProcessFloat64;
	if (facts.inputFormat == facts.outputFormat && facts.inputFormat == SampleFormat::Float32)
		return BlockAction::ProcessFloat32;

	return facts.inPlace ? BlockAction::PassThroughInPlace : BlockAction::SilenceDistinctBuffers;
}

inline BlockPlan makeBlockPlan(const BlockFacts& facts, unsigned frameCount)
{
	BlockPlan plan;
	plan.action = chooseBlockAction(facts);
	plan.frameCount = frameCount;
	plan.outputBytesPerSample = bytesPerSample(facts.outputFormat);
	plan.inputFlags = facts.inputFlags;

	switch (plan.action)
	{
	case BlockAction::Ignore:
		plan.outputFlagRule = OutputFlagRule::Preserve;
		break;
	case BlockAction::SilentFastPath:
	case BlockAction::SilenceDistinctBuffers:
		plan.outputFlagRule = OutputFlagRule::Silent;
		break;
	case BlockAction::PassThroughInPlace:
		plan.outputFlagRule = OutputFlagRule::CopyInput;
		break;
	case BlockAction::ProcessFloat32:
	case BlockAction::ProcessFloat64:
		plan.zeroInputFirst = facts.inputFlags == BUFFER_SILENT;
		plan.processChildFirst = facts.hasChildApo;
		if (facts.inputFlags != BUFFER_SILENT)
			plan.outputFlagRule = OutputFlagRule::Valid;
		else if (facts.allowSilentBufferModification)
			plan.outputFlagRule = OutputFlagRule::InspectProcessedOutput;
		else
			plan.outputFlagRule = OutputFlagRule::Silent;
		break;
	}
	return plan;
}

struct BlockResult
{
	bool updateOutputProperties = false;
	unsigned validFrameCount = 0;
	APO_BUFFER_FLAGS outputFlags = BUFFER_INVALID;
};

template<typename SampleT, typename EngineLike, typename ChildProcessor>
inline BlockResult processBlock(const BlockPlan& plan,
	SampleT* inputFrames, SampleT* outputFrames,
	unsigned inputChannelCount, unsigned outputChannelCount,
	EngineLike& engine, ChildProcessor&& processChild)
{
	if (plan.action == BlockAction::Ignore)
		return {};

	if (plan.action == BlockAction::SilentFastPath
		|| plan.action == BlockAction::SilenceDistinctBuffers)
	{
		if (plan.outputBytesPerSample > 0)
		{
			std::memset(static_cast<void*>(outputFrames), 0,
				static_cast<size_t>(plan.frameCount) * outputChannelCount
					* plan.outputBytesPerSample);
		}
	}
	else if (plan.action == BlockAction::ProcessFloat32
		|| plan.action == BlockAction::ProcessFloat64)
	{
		if (plan.zeroInputFirst)
		{
			std::fill_n(inputFrames,
				static_cast<size_t>(plan.frameCount) * inputChannelCount,
				static_cast<SampleT>(0));
		}

		if (plan.processChildFirst)
		{
			std::forward<ChildProcessor>(processChild)();
			engine.process(outputFrames, outputFrames, plan.frameCount);
		}
		else
		{
			engine.process(outputFrames, inputFrames, plan.frameCount);
		}
	}

	BlockResult result;
	result.updateOutputProperties = true;
	result.validFrameCount = plan.frameCount;
	switch (plan.outputFlagRule)
	{
	case OutputFlagRule::Preserve:
		result.updateOutputProperties = false;
		break;
	case OutputFlagRule::Silent:
		// A processed silent block may contain filter output that the host does
		// not permit us to expose. The two non-processing silence actions were
		// already cleared byte-for-byte above using their actual container size.
		if (plan.action == BlockAction::ProcessFloat32
			|| plan.action == BlockAction::ProcessFloat64)
		{
			std::fill_n(outputFrames,
				static_cast<size_t>(plan.frameCount) * outputChannelCount,
				static_cast<SampleT>(0));
		}
		result.outputFlags = BUFFER_SILENT;
		break;
	case OutputFlagRule::Valid:
		result.outputFlags = BUFFER_VALID;
		break;
	case OutputFlagRule::InspectProcessedOutput:
		result.outputFlags = isBlockSilent(outputFrames,
			static_cast<size_t>(plan.frameCount) * outputChannelCount)
			? BUFFER_SILENT : BUFFER_VALID;
		break;
	case OutputFlagRule::CopyInput:
		result.outputFlags = plan.inputFlags == BUFFER_SILENT
			? BUFFER_SILENT : BUFFER_VALID;
		break;
	}
	return result;
}

// The endpoint's channel mask, taken from the connection this instance
// processes (input for capture, output for render), with a fallback to the
// opposite side when the preferred mask is zero and the channel counts agree -
// some drivers only fill one side in.
inline unsigned resolveChannelMask(bool capture,
	unsigned inputMask, unsigned inputChannelCount,
	unsigned outputMask, unsigned outputChannelCount)
{
	unsigned mask = capture ? inputMask : outputMask;
	if (mask == 0 && inputChannelCount == outputChannelCount)
		mask = capture ? outputMask : inputMask;
	return mask;
}

} // namespace apo
