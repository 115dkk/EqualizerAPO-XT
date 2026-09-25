/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <cmath>
#include <cstddef>

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

// What APOProcess does with one block (audit #348 F20). The choice used to be
// nested ifs in the COM method, where no test could reach it, and a wrong
// branch there is how an endpoint goes mute or plays unprocessed audio.
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
