/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "EqualizerAPOHost/KeepaliveFill.h"
#include "devices/DeviceAPOInfoKeys.h"
#include "devices/ReceiverEndpoints.h"
#include "Tests/FakeRegistry.h"
#include "Tests/TestHarness.h"

#include <ks.h>
#include <ksmedia.h>

namespace
{
	test::Harness harness("ReceiverKeepaliveTests");

	void testReceivingEndpoints()
	{
		test::FakeRegistry registry;
		harness.expect(receivingEndpoints(registry).empty(), "missing Child APOs is empty");
		const std::wstring first = L"{a6974eef-cbb1-4e81-b9ca-34b91fff5279}";
		const std::wstring second = L"{466A3ACF-0324-46F9-9A38-FB08FFDD208E}";
		const std::wstring child = std::wstring(childApoPath) + L"\\" + first;
		registry.seedKey(child);
		registry.seedKey(std::wstring(renderKeyPath) + L"\\" + first);
		harness.expect(receivingEndpoints(registry).empty(), "missing option is off");
		for (const wchar_t* value : {L"true", L"TRUE", L"1", L"", L"FALSE"})
		{
			registry.seedString(child, receiveFromEndpointsValueName, value);
			const auto found = receivingEndpoints(registry);
			harness.requireEqual(found.size(), size_t(1), "anything except lowercase false enables receiving");
			harness.expect(found.front() == first, "the endpoint subkey spelling is preserved");
		}
		registry.seedString(child, receiveFromEndpointsValueName, L"false");
		harness.expect(receivingEndpoints(registry).empty(), "lowercase false disables receiving");
		registry.seedString(std::wstring(childApoPath) + L"\\" + second, receiveFromEndpointsValueName, L"true");
		registry.seedKey(std::wstring(captureKeyPath) + L"\\" + second);
		harness.expect(receivingEndpoints(registry).empty(), "capture-only endpoint is excluded");
		registry.seedKey(std::wstring(renderKeyPath) + L"\\" + second);
		registry.seedDword(child, receiveFromEndpointsValueName, 1);
		const auto found = receivingEndpoints(registry);
		harness.requireEqual(found.size(), size_t(1), "a wrong-type record is skipped without losing a valid receiver");
		harness.expect(found.front() == second, "the other receiver survives the read failure");
		registry.denyRead(child);
		harness.expectEqual(receivingEndpoints(registry).size(), size_t(1), "an inaccessible subkey is skipped");
		registry.denyRead(childApoPath);
		harness.expect(receivingEndpoints(registry).empty(), "an inaccessible root does not throw");
	}

	WAVEFORMATEXTENSIBLE makeFormat(unsigned bits, bool floating, bool extensible, unsigned validBits = 0)
	{
		WAVEFORMATEXTENSIBLE format = {};
		format.Format.wFormatTag = extensible ? WAVE_FORMAT_EXTENSIBLE : (floating ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM);
		format.Format.nChannels = 3;
		format.Format.nSamplesPerSec = 48000;
		format.Format.wBitsPerSample = static_cast<WORD>(bits);
		format.Format.nBlockAlign = static_cast<WORD>(format.Format.nChannels * bits / 8);
		format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
		format.Format.cbSize = extensible ? sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) : 0;
		format.Samples.wValidBitsPerSample = static_cast<WORD>(validBits == 0 ? bits : validBits);
		format.SubFormat = floating ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
		return format;
	}

	void checkFill(const WAVEFORMATEXTENSIBLE& format, bool floating, bool dither)
	{
		constexpr UINT32 frames = 4096;
		constexpr size_t guard = 19;
		constexpr BYTE sentinel = 0xa5;
		const size_t bytes = static_cast<size_t>(frames) * format.Format.nBlockAlign;
		std::vector<BYTE> storage(bytes + 2 * guard, sentinel);
		BYTE* data = storage.data() + guard;
		uint32_t rng = 0;
		harness.require(keepaliveFormatSupported(format.Format), "test format is supported");
		fillKeepalive(data, frames, format.Format, dither, rng);
		harness.expect(std::all_of(storage.begin(), storage.begin() + guard, [](BYTE value) { return value == sentinel; })
			&& std::all_of(storage.end() - guard, storage.end(), [](BYTE value) { return value == sentinel; }),
			"fill leaves both guard regions untouched, including an unaligned destination");
		if (!dither)
		{
			harness.expect(std::all_of(data, data + bytes, [](BYTE value) { return value == 0; }), "zero mode writes every byte as zero");
			harness.expectEqual(rng, uint32_t(0), "zero mode does not advance the generator");
			return;
		}

		bool bounded = true;
		bool positive = false;
		bool negative = false;
		bool aligned = true;
		const unsigned sampleBytes = format.Format.wBitsPerSample / 8;
		const unsigned validBits = format.Samples.wValidBitsPerSample;
		const unsigned shift = format.Format.wBitsPerSample - validBits;
		const double scale = std::ldexp(1.0, static_cast<int>(validBits) - 1);
		for (size_t i = 0; i < static_cast<size_t>(frames) * format.Format.nChannels; i++)
		{
			double value;
			if (floating)
			{
				float decoded;
				std::memcpy(&decoded, data + i * sampleBytes, sizeof(decoded));
				value = decoded;
				bounded = bounded && std::isfinite(value) && std::abs(value) <= 2e-5;
			}
			else
			{
				uint32_t raw = 0;
				for (unsigned b = 0; b < sampleBytes; b++)
					raw |= static_cast<uint32_t>(data[i * sampleBytes + b]) << (b * 8);
				const unsigned bits = format.Format.wBitsPerSample;
				const int64_t decoded = (raw & (uint32_t(1) << (bits - 1))) != 0
					? static_cast<int64_t>(raw) - (int64_t(1) << bits) : raw;
				const double sample = static_cast<double>(decoded) / std::ldexp(1.0, static_cast<int>(shift));
				value = sample / scale;
				bounded = bounded && std::abs(sample) <= (validBits == 16 ? 1.0 : std::ceil(1e-5 * scale));
				aligned = aligned && (raw & ((uint32_t(1) << shift) - 1)) == 0;
			}
			positive = positive || value > 0;
			negative = negative || value < 0;
		}
		harness.expect(bounded, "dither stays within the float or PCM quantization bound");
		harness.expect(positive && negative, "dither contains both signs and is not all zero, including PCM16");
		harness.expect(aligned, "unused extensible PCM low bits remain zero");
	}

	void testKeepaliveZeroFill()
	{
		for (bool extensible : {false, true})
		{
			checkFill(makeFormat(32, true, extensible), true, false);
			for (unsigned bits : {16u, 24u, 32u})
				checkFill(makeFormat(bits, false, extensible), false, false);
		}
	}

	void testKeepaliveDitherFill()
	{
		for (bool extensible : {false, true})
		{
			checkFill(makeFormat(32, true, extensible), true, true);
			for (unsigned bits : {16u, 24u, 32u})
				checkFill(makeFormat(bits, false, extensible), false, true);
		}
		checkFill(makeFormat(32, false, true, 24), false, true);
		checkFill(makeFormat(24, false, true, 20), false, true);
	}

	void testKeepaliveEmptyAndUnsupportedFill()
	{
		WAVEFORMATEXTENSIBLE format = makeFormat(32, true, true);
		BYTE guard = 0xa5;
		uint32_t rng = 0;
		fillKeepalive(&guard, 0, format.Format, true, rng);
		harness.expect(guard == 0xa5 && rng == 0, "zero frames change neither storage nor generator");
		format.Format.cbSize = 0;
		harness.expectFalse(keepaliveFormatSupported(format.Format), "truncated extensible format is refused");
		std::vector<BYTE> data(format.Format.nBlockAlign + 1, 0xa5);
		fillKeepalive(data.data(), 1, format.Format, true, rng);
		harness.expect(std::all_of(data.begin(), data.end() - 1, [](BYTE value) { return value == 0; })
			&& data.back() == 0xa5, "unsupported format is zeroed without overrunning");
		format = makeFormat(32, true, false);
		format.Format.nBlockAlign--;
		harness.expectFalse(keepaliveFormatSupported(format.Format), "bad block alignment is refused");
	}
}

int runReceiverKeepaliveTests()
{
	testReceivingEndpoints();
	testKeepaliveZeroFill();
	testKeepaliveDitherFill();
	testKeepaliveEmptyAndUnsupportedFill();
	harness.report();
	return 0;
}
