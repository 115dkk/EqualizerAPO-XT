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

} // namespace

// Called from the suite's main() in EngineOrchestrationTests.cpp.
void runApoFormatTests(test::Harness& harness)
{
	testFormatDetection(harness);
	testSilenceVerdict(harness);
	testChannelMaskFallback(harness);
}
