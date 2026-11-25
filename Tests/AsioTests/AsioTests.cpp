/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Unit tests for the ASIO wrapper's core: the sample codecs, the callback
	trampolines, the wrapper's state machine and per-switch data flow over
	the statically linked fake driver, the registry record, and the
	in-process engine adapter. No DLL is loaded and no registry key is
	touched; the fake driver is stepped on this thread.
*/

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
#include <timeapi.h>

#include "services/shell/UnelevatedLaunch.h"
#include "asio/AsioWrapper.h"
#include "asio/CallbackTrampolines.h"
#include "asio/DriverNameText.h"
#include "asio/InProcProcessor.h"
#include "asio/SampleCodec.h"
#include "asio/StreamProcessor.h"
#include "asio/WasapiExclusiveTarget.h"
#include "asio/WaveFormat.h"
#include "asio/WrapperRecord.h"
#include "services/logging/Logging.h"
#include "Tests/AlignedMemoryGate.h"
#include "Tests/AsioSupport/HostStub.h"
#include "Tests/AsioSupport/LatencyMatch.h"
#include "Tests/AsioSupport/SampleOracle.h"
#include "Tests/AsioTests/FakeEndpointClient.h"
#include "Tests/FakeRegistry.h"
#include "Tests/FakeAsioDriver/FakeAsio.h"
#include "Tests/TestDirectory.h"
#include "../TestHarness.h"

using eapo::asio::AsioWrapper;
using eapo::asio::Direction;
using eapo::asio::IStreamProcessor;
using eapo::asio::Mode;
using eapo::asio::OpenReport;
using eapo::asio::Outcome;
using eapo::asio::PassthroughProcessor;
using eapo::asio::SampleCodec;
using eapo::asio::StreamFormat;
using eapo::asio::StreamOptions;
using eapo::asio::StreamStats;

namespace
{
	test::HarnessAccess harness;

	template<typename Predicate>
	bool waitUntil(Predicate predicate, DWORD timeoutMs)
	{
		const ULONGLONG deadline = GetTickCount64() + timeoutMs;
		for (;;)
		{
			if (predicate())
				return true;
			if (GetTickCount64() >= deadline)
				return false;
			Sleep(1);
		}
	}

	// {C0FFEE00-1111-4222-8333-444455556666}: the CLSID the tests build wrappers under.
	constexpr GUID testWrapperClsid = {0xc0ffee00, 0x1111, 0x4222, {0x83, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66}};
	const wchar_t* const testTargetClsid = L"{B7E3A9F4-52C1-4D0B-8A6E-1F9C3D5E7B21}";

	// A processor that multiplies by a gain and follows a script of outcomes.
	class ScriptedProcessor final : public IStreamProcessor
	{
	public:
		float gain = 0.5f;
		std::vector<Outcome> script;           // consumed per process() call, any direction
		OpenReport::Status openStatus = OpenReport::Status::Ok;
		uint32_t extraLatency = 0;
		bool throwOnOpen = false;
		bool throwUnknownOnOpen = false;
		FakeAsioDriver* enterCallbackFrom = nullptr;
		ASIOCallbacks* enteredCallback = nullptr;
		bool callbackEntered = false;
		unsigned opens = 0, closes = 0, calls = 0;
		StreamFormat openedWith;

		OpenReport open(const StreamFormat& format, const StreamOptions&) override
		{
			opens++;
			openedWith = format;
			if (enterCallbackFrom != nullptr)
			{
				enteredCallback = enterCallbackFrom->callbackSetForTesting();
				callbackEntered = eapo::asio::CallbackTrampolines::testEnter(enteredCallback);
			}
			if (throwOnOpen)
				throw std::runtime_error("scripted open exception");
			if (throwUnknownOnOpen)
				throw 7;
			OpenReport report;
			report.status = openStatus;
			if (openStatus != OpenReport::Status::Ok)
			{
				std::memcpy(report.message, "scripted failure", 17);
				return report;
			}
			for (unsigned slot = 0; slot < eapo::asio::directionCount; slot++)
			{
				const unsigned channels = format.channels[slot];
				storage_[slot].assign(static_cast<size_t>(channels) * format.frames, 0.0);
				planes_[slot].resize(channels);
				for (unsigned c = 0; c < channels; c++)
					planes_[slot][c] = storage_[slot].data() + static_cast<size_t>(c) * format.frames;
			}
			report.extraLatencyFrames = extraLatency;
			return report;
		}

		double** planes(Direction direction) noexcept override
		{
			return planes_[eapo::asio::slotOf(direction)].empty() ? nullptr : planes_[eapo::asio::slotOf(direction)].data();
		}

		Outcome process(Direction direction) noexcept override
		{
			const unsigned index = calls++;
			if (index < script.size() && script[index] != Outcome::Processed)
				return script[index];
			const unsigned slot = static_cast<unsigned>(direction);
			for (double& sample : storage_[slot])
				sample *= static_cast<double>(gain);
			return Outcome::Processed;
		}

		void close() noexcept override
		{
			closes++;
		}

	private:
		std::vector<double> storage_[eapo::asio::directionCount];
		std::vector<double*> planes_[eapo::asio::directionCount];
	};

	struct Rig
	{
		FakeAsioDriver* fake;
		ScriptedProcessor* processor;
		AsioWrapper* wrapper;
		std::unique_ptr<asiotest::HostStub> host;

		Rig(FakeAsioConfig config, StreamOptions options, asiotest::HostStub::Options hostOptions, ScriptedProcessor* scripted = nullptr)
		{
			fake = new FakeAsioDriver();
			fake->configure(&config);
			// The wrapper owns the processor from here on; the raw pointer is
			// how the test scripts and inspects it.
			processor = scripted != nullptr ? scripted : new ScriptedProcessor();
			const bool float64Samples = options.float64Samples;
			wrapper = new AsioWrapper(fake, testWrapperClsid, testTargetClsid, std::move(options), std::unique_ptr<IStreamProcessor>(processor));
			fake->Release();       // the wrapper holds the reference now
			hostOptions.sampleType = float64Samples ? ASIOSTFloat64LSB : config.sampleType;
			host = std::make_unique<asiotest::HostStub>(hostOptions);
		}

		~Rig()
		{
			host.reset();
			wrapper->Release();
		}

		Rig(const Rig&) = delete;
		Rig& operator=(const Rig&) = delete;

		IFakeAsioControl* control()
		{
			return fake;
		}

		FakeAsioCounters counters()
		{
			FakeAsioCounters c;
			fake->counters(&c);
			return c;
		}
	};

	std::vector<ASIOBufferInfo> outputInfos(long count)
	{
		std::vector<ASIOBufferInfo> infos(static_cast<size_t>(count));
		for (long i = 0; i < count; i++)
		{
			infos[static_cast<size_t>(i)].isInput = ASIOFalse;
			infos[static_cast<size_t>(i)].channelNum = i;
		}
		return infos;
	}

	std::vector<float> decode(const SampleCodec& codec, const std::vector<unsigned char>& bytes)
	{
		std::vector<double> decoded(bytes.size() / codec.bytesPerSample);
		codec.toDouble(bytes.data(), decoded.data(), static_cast<unsigned>(decoded.size()));
		std::vector<float> samples(decoded.size());
		for (size_t i = 0; i < decoded.size(); i++)
			samples[i] = static_cast<float>(decoded[i]);
		return samples;
	}

	std::vector<unsigned char> captured(IFakeAsioControl* control, long channel)
	{
		const unsigned char* data = nullptr;
		unsigned long bytes = 0;
		control->capturedOutput(channel, &data, &bytes);
		return std::vector<unsigned char>(data, data + bytes);
	}

	std::vector<unsigned char> supplied(IFakeAsioControl* control, long channel)
	{
		const unsigned char* data = nullptr;
		unsigned long bytes = 0;
		control->suppliedInput(channel, &data, &bytes);
		return std::vector<unsigned char>(data, data + bytes);
	}

	std::vector<double> oracleDecoded(long type, const std::vector<unsigned char>& bytes)
	{
		const size_t sampleBytes = asiotest::oracle::bytesPerSample(type);
		harness.require(sampleBytes != 0 && bytes.size() % sampleBytes == 0, "oracle input has whole supported samples");
		std::vector<double> values(bytes.size() / sampleBytes);
		asiotest::oracle::decode(type, bytes.data(), values.data(), values.size());
		return values;
	}

	std::vector<unsigned char> oracleEncoded(long type, const std::vector<double>& values)
	{
		const size_t sampleBytes = asiotest::oracle::bytesPerSample(type);
		harness.require(sampleBytes != 0, "oracle output has a supported sample type");
		std::vector<unsigned char> bytes(values.size() * sampleBytes);
		asiotest::oracle::encode(type, values.data(), bytes.data(), values.size());
		return bytes;
	}

	void expectExactBytes(const std::vector<unsigned char>& actual, const std::vector<unsigned char>& expected,
		const std::string& label)
	{
		harness.requireEqual(actual.size(), expected.size(), label + " byte count");
		harness.expect(actual == expected, label);
	}

	// Applies the codec round trip the host's float takes before the wrapper
	// sees it, so expectations quantize the same way the buffers do.
	float quantized(const SampleCodec& codec, float value)
	{
		unsigned char bytes[8] = {};
		const double source = static_cast<double>(value);
		double back = 0.0;
		codec.fromDouble(&source, bytes, 1);
		codec.toDouble(bytes, &back, 1);
		return static_cast<float>(back);
	}

	// ---- codecs ----

	TEST_CASE(AsioTests, testHelperLaunchDropsTheAdministratorToken)
	{
		int shellCalls = 0;
		int plainCalls = 0;
		const auto shell = [&]() { ++shellCalls; return false; };
		const auto plain = [&]() { ++plainCalls; return true; };
		harness.expectTrue(!winutil::chooseUnelevatedLaunch([]() { return true; }, shell, plain),
			"elevated helper uses shell launch and propagates failure");
		harness.expectTrue(shellCalls == 1 && plainCalls == 0,
			"missing shell never falls back to elevated launch");
		harness.expectTrue(winutil::chooseUnelevatedLaunch([]() { return false; }, shell, plain),
			"unelevated helper uses plain launch");
		harness.expectTrue(shellCalls == 1 && plainCalls == 1, "each launch chooses one token path");
	}

	TEST_CASE(AsioTests, testCodecRoundTrips)
	{
		const long types[] = {
			ASIOSTInt16LSB, ASIOSTInt16MSB, ASIOSTInt24LSB, ASIOSTInt24MSB, ASIOSTInt32LSB, ASIOSTInt32MSB,
			ASIOSTFloat32LSB, ASIOSTFloat32MSB, ASIOSTFloat64LSB, ASIOSTFloat64MSB,
			ASIOSTInt32LSB16, ASIOSTInt32LSB18, ASIOSTInt32LSB20, ASIOSTInt32LSB24,
			ASIOSTInt32MSB16, ASIOSTInt32MSB18, ASIOSTInt32MSB20, ASIOSTInt32MSB24
		};
		const double probe[] = {0.0, 0.5, -0.5, 0.999, -1.0, 0.123456, -0.000123, 1.5, -2.0};
		for (long type : types)
		{
			SampleCodec codec;
			harness.require(eapo::asio::findSampleCodec(type, codec), "codec exists for type " + std::to_string(type));
			harness.expectEqual(codec.type, type, "codec reports its own type");
			std::vector<unsigned char> bytes(codec.bytesPerSample * 9);
			double back[9] = {};
			codec.fromDouble(probe, bytes.data(), 9);
			codec.toDouble(bytes.data(), back, 9);
			const double tolerance = type == ASIOSTInt16LSB || type == ASIOSTInt16MSB
				|| type == ASIOSTInt32LSB16 || type == ASIOSTInt32MSB16 ? 1.0 / 32768.0 : 1.0 / 131072.0;
			const bool floating = type == ASIOSTFloat32LSB || type == ASIOSTFloat32MSB
				|| type == ASIOSTFloat64LSB || type == ASIOSTFloat64MSB;
			for (int i = 0; i < 9; i++)
			{
				double expected = probe[i];
				// Integer types clamp at full scale; float types carry
				// over-range values through untouched.
				if (!floating && expected > 1.0)
					expected = 1.0;
				if (!floating && expected < -1.0)
					expected = -1.0;
				harness.expectNear(back[i], expected, tolerance,
					"type " + std::to_string(type) + " round-trips sample " + std::to_string(i));
			}
		}

		SampleCodec dsd;
		harness.expectFalse(eapo::asio::findSampleCodec(ASIOSTDSDInt8LSB1, dsd), "DSD has no codec");
		harness.expectFalse(eapo::asio::findSampleCodec(12345, dsd), "an unknown type has no codec");
	}

	TEST_CASE(AsioTests, testCodecMatchesIndependentOracle)
	{
		const long types[] = {
			ASIOSTInt16LSB, ASIOSTInt24LSB, ASIOSTInt32LSB, ASIOSTFloat32LSB, ASIOSTFloat64LSB
		};
		asiotest::HostStub::Options options;
		options.fullPrecision = true;
		asiotest::HostStub signal(options);
		for (long type : types)
		{
			std::vector<double> values = {
				0.0, 1.0, -1.0, 1.5, -1.5,
				std::nextafter(1.0, 0.0), std::nextafter(1.0, 2.0),
				std::nextafter(-1.0, 0.0), std::nextafter(-1.0, -2.0)
			};
			unsigned validBits = 0;
			if (type == ASIOSTInt16LSB)
				validBits = 16;
			else if (type == ASIOSTInt24LSB)
				validBits = 24;
			else if (type == ASIOSTInt32LSB)
				validBits = 32;
			if (validBits != 0)
			{
				const double step = 1.0 / static_cast<double>(UINT64_C(1) << (validBits - 1));
				values.insert(values.end(), {step, -step, 0.5 * step, -0.5 * step,
					1.5 * step, -1.5 * step, 2.5 * step, -2.5 * step});
			}
			for (size_t i = 0; i < 4096; i++)
				values.push_back(signal.outputValue(static_cast<long>(i & 1), i));

			SampleCodec codec;
			harness.require(eapo::asio::findSampleCodec(type, codec),
				"the oracle sweep has a product codec for type " + std::to_string(type));
			const size_t byteCount = values.size() * codec.bytesPerSample;
			std::vector<unsigned char> productBytes(byteCount);
			std::vector<unsigned char> oracleBytes(byteCount);
			codec.fromDouble(values.data(), productBytes.data(), static_cast<unsigned>(values.size()));
			asiotest::oracle::encode(type, values.data(), oracleBytes.data(), values.size());
			harness.expect(productBytes == oracleBytes,
				"the product encoder matches the independent oracle for type " + std::to_string(type));

			std::vector<double> productValues(values.size());
			std::vector<double> oracleValues(values.size());
			codec.toDouble(oracleBytes.data(), productValues.data(), static_cast<unsigned>(productValues.size()));
			asiotest::oracle::decode(type, oracleBytes.data(), oracleValues.data(), oracleValues.size());
			harness.expect(productValues == oracleValues,
				"the product decoder matches the independent oracle for type " + std::to_string(type));
		}
	}

	TEST_CASE(AsioTests, testCodecKeepsEveryRepresentableSourceBit)
	{
		const auto integerCodesSurvive = [](long type, const std::vector<int32_t>& codes, unsigned bytesPerSample) {
			SampleCodec codec;
			if (!eapo::asio::findSampleCodec(type, codec))
				return false;
			std::vector<unsigned char> source(codes.size() * bytesPerSample);
			for (size_t i = 0; i < codes.size(); i++)
			{
				const uint32_t raw = static_cast<uint32_t>(codes[i]);
				std::memcpy(source.data() + i * bytesPerSample, &raw, bytesPerSample);
			}
			std::vector<double> values(codes.size());
			std::vector<unsigned char> encoded(source.size());
			codec.toDouble(source.data(), values.data(), static_cast<unsigned>(values.size()));
			codec.fromDouble(values.data(), encoded.data(), static_cast<unsigned>(values.size()));
			return source == encoded;
		};

		std::vector<int32_t> int32Codes = {0, 1, -1, INT32_MAX, INT32_MIN};
		for (uint64_t i = 0; i < 4096; i++)
		{
			const int64_t code = static_cast<int64_t>(INT32_MIN)
				+ static_cast<int64_t>(i * UINT32_MAX / 4095);
			int32Codes.push_back(static_cast<int32_t>(code));
		}
		harness.expect(integerCodesSurvive(ASIOSTInt32LSB, int32Codes, 4),
			"every sampled Int32LSB code survives decode and encode unchanged");
		harness.expect(integerCodesSurvive(ASIOSTInt24LSB,
			{0, 1, -1, 0x007fffff, static_cast<int32_t>(0xff800000u)}, 3),
			"the Int24LSB edge codes survive decode and encode unchanged");
		harness.expect(integerCodesSurvive(ASIOSTInt16LSB,
			{0, 1, -1, 0x00007fff, static_cast<int32_t>(0xffff8000u)}, 2),
			"the Int16LSB edge codes survive decode and encode unchanged");

		SampleCodec float64;
		harness.require(eapo::asio::findSampleCodec(ASIOSTFloat64LSB, float64), "the Float64LSB codec exists");
		const double float64Values[] = {
			0.0, -0.0, std::numeric_limits<double>::denorm_min(), 1.5,
			std::bit_cast<double>(UINT64_C(0x3fffffffffffffff))
		};
		std::array<unsigned char, sizeof(float64Values)> sourceBytes = {};
		std::array<unsigned char, sizeof(float64Values)> encodedBytes = {};
		std::memcpy(sourceBytes.data(), float64Values, sizeof(float64Values));
		double decoded[sizeof(float64Values) / sizeof(float64Values[0])] = {};
		float64.toDouble(sourceBytes.data(), decoded, static_cast<unsigned>(std::size(decoded)));
		float64.fromDouble(decoded, encodedBytes.data(), static_cast<unsigned>(std::size(decoded)));
		harness.expect(sourceBytes == encodedBytes,
			"Float64LSB keeps denormal, over-range and full-mantissa values bit for bit");
	}

	TEST_CASE(AsioTests, testCodecEncodesIntegerNaNAsZero)
	{
		const long types[] = {ASIOSTInt16LSB, ASIOSTInt24LSB, ASIOSTInt32LSB};
		const double value = std::numeric_limits<double>::quiet_NaN();
		for (long type : types)
		{
			SampleCodec codec;
			harness.require(eapo::asio::findSampleCodec(type, codec),
				"the NaN test has a codec for type " + std::to_string(type));
			std::array<unsigned char, 4> bytes = {0xcd, 0xcd, 0xcd, 0xcd};
			codec.fromDouble(&value, bytes.data(), 1);
			bool zero = true;
			for (unsigned i = 0; i < codec.bytesPerSample; i++)
				zero = zero && bytes[i] == 0;
			harness.expect(zero, "NaN encodes as zero for integer type " + std::to_string(type));
		}
	}

	// The WASAPI target's pure parts: which containers are tried in which
	// order, what buffer sizes it offers, and the byte shuffles between the
	// ASIO planes and the device's interleaved block.
	TEST_CASE(AsioTests, testWasapiTargetPolicy)
	{
		namespace wasapi = eapo::asio::wasapi;

		// The endpoint's own format leads; the fixed list follows without repeats.
		WAVEFORMATEXTENSIBLE sixteen = wasapi::makeFormat(wasapi::Container{ASIOSTInt16LSB, 16, 16, false}, 2, 48000, 3);
		std::vector<wasapi::Container> order = wasapi::containerCandidates(&sixteen.Format, sizeof(sixteen));
		harness.require(order.size() == 5, "five containers are tried for a 16-bit device format");
		harness.expectEqual(order[0].asioType, static_cast<long>(ASIOSTInt16LSB), "the device's own container comes first");
		harness.expectEqual(order[1].asioType, static_cast<long>(ASIOSTFloat32LSB), "float follows");
		harness.expectEqual(order[2].asioType, static_cast<long>(ASIOSTInt32LSB), "then 32 bit");
		harness.expectEqual(order[3].asioType, static_cast<long>(ASIOSTInt32LSB24), "then 24 in 32");
		harness.expectEqual(order[4].asioType, static_cast<long>(ASIOSTInt24LSB), "then packed 24; 16 bit is not repeated");

		WAVEFORMATEXTENSIBLE twentyFour = wasapi::makeFormat(wasapi::Container{ASIOSTInt32LSB24, 32, 24, false}, 2, 96000, 3);
		order = wasapi::containerCandidates(&twentyFour.Format, sizeof(twentyFour));
		harness.require(order.size() == 5, "five containers for a 24-in-32 device format");
		harness.expectEqual(order[0].asioType, static_cast<long>(ASIOSTInt32LSB24), "24 valid bits in 32 is recognised from the extensible header");
		harness.expectEqual(order[4].asioType, static_cast<long>(ASIOSTInt16LSB), "16 bit closes the list");

		order = wasapi::containerCandidates(nullptr, 0);
		harness.expectEqual(order.size(), static_cast<size_t>(5), "no device format: the fixed list alone");
		harness.expectEqual(order[0].asioType, static_cast<long>(ASIOSTFloat32LSB), "which starts with float");

		WAVEFORMATEX plain = {};
		plain.wFormatTag = WAVE_FORMAT_PCM;
		plain.nChannels = 2;
		plain.nSamplesPerSec = 44100;
		plain.wBitsPerSample = 24;
		order = wasapi::containerCandidates(&plain, sizeof(plain));
		harness.expectEqual(order[0].asioType, static_cast<long>(ASIOSTInt24LSB), "a plain 24-bit PCM header is packed 24");

		// Audit #467 TD-52: the blob occupies this exact allocation, so reading
		// any WAVEFORMATEXTENSIBLE field would cross its end under ASan.
		auto shortBlob = std::make_unique<unsigned char[]>(sizeof(WAVEFORMATEX));
		std::memset(shortBlob.get(), 0, sizeof(WAVEFORMATEX));
		auto* shortExtensible = reinterpret_cast<WAVEFORMATEX*>(shortBlob.get());
		shortExtensible->wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		shortExtensible->wBitsPerSample = 32;
		shortExtensible->cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
		eapo::asio::waveformat::Container shortDescription;
		harness.expectFalse(eapo::asio::waveformat::containerOf(shortExtensible, sizeof(WAVEFORMATEX), shortDescription),
			"an extensible tag in a short blob is rejected without reading extension fields");

		// The extensible format a container becomes.
		harness.expectEqual(static_cast<int>(twentyFour.Format.nBlockAlign), 8, "24 in 32 on two channels is 8 bytes a frame");
		harness.expectEqual(static_cast<int>(twentyFour.Samples.wValidBitsPerSample), 24, "valid bits carried into the header");
		harness.expectTrue(twentyFour.SubFormat == KSDATAFORMAT_SUBTYPE_PCM, "integer containers are PCM");
		WAVEFORMATEXTENSIBLE floating = wasapi::makeFormat(wasapi::Container{ASIOSTFloat32LSB, 32, 32, true}, 8, 48000, 0x63f);
		harness.expectTrue(floating.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, "float is IEEE float");
		harness.expectEqual(static_cast<unsigned long>(floating.dwChannelMask), 0x63ful, "the channel mask is passed through");
		harness.expectEqual(floating.Format.nAvgBytesPerSec, 48000u * 32u, "bytes per second follow the layout");

		// Buffer sizes: the device's own smallest power of two is preferred;
		// down to an eighth of it the device runs at that size and serves
		// several buffers per period.
		wasapi::BufferPolicy policy = wasapi::bufferPolicy(144);       // 3 ms at 48 kHz
		harness.expectEqual(policy.preferredSize, 256L, "3 ms at 48 kHz prefers 256 frames, the smallest the device takes");
		harness.expectEqual(policy.minSize, 32L, "and offers down to an eighth of it");
		harness.expectEqual(policy.maxSize, 2048L, "up to 2048");
		harness.expectEqual(policy.granularity, -1L, "in powers of two");
		policy = wasapi::bufferPolicy(96);
		harness.expectEqual(policy.preferredSize, 128L, "2 ms at 48 kHz prefers 128");
		harness.expectEqual(policy.minSize, 32L, "never below 32");
		policy = wasapi::bufferPolicy(0);
		harness.expectEqual(policy.preferredSize, 32L, "no minimum prefers 32");
		harness.expectEqual(policy.minSize, 32L, "and offers 32");
		policy = wasapi::bufferPolicy(4000);
		harness.expectEqual(policy.preferredSize, 4096L, "a huge minimum is honoured");
		harness.expectEqual(policy.minSize, 512L, "an eighth of it is the smallest offered");
		harness.expectEqual(policy.maxSize, 4096L, "and the maximum follows it up");
		const unsigned minPeriod48 = wasapi::framesFromHns(30000, 48000);
		harness.expectEqual(minPeriod48, 144u, "a 3 ms minimum is 144 frames at 48 kHz");
		harness.expectEqual(wasapi::bufferPolicy(minPeriod48).preferredSize, 256L, "the 48 kHz minimum prefers 256 frames");
		const unsigned minPeriod96 = wasapi::framesFromHns(30000, 96000);
		harness.expectEqual(minPeriod96, 288u, "the same 3 ms minimum is 288 frames at 96 kHz");
		harness.expectEqual(wasapi::bufferPolicy(minPeriod96).preferredSize, 512L, "the 96 kHz minimum prefers 512 frames");
		harness.expectEqual(wasapi::bufferPolicy(minPeriod96).minSize, 64L, "and offers down to 64");

		// The device period a buffer opens with, in buffers.
		harness.expectEqual(wasapi::initialBridge(256, 256), 1u, "the preferred size needs no bridge");
		harness.expectEqual(wasapi::initialBridge(1024, 256), 1u, "nor does a larger buffer");
		harness.expectEqual(wasapi::initialBridge(128, 256), 2u, "a game's 128 on a 3 ms device: two buffers per 256-frame period");
		harness.expectEqual(wasapi::initialBridge(64, 256), 4u, "64: four");
		harness.expectEqual(wasapi::initialBridge(32, 256), 8u, "32: eight, the cap");
		harness.expectEqual(wasapi::initialBridge(96, 256), 3u, "a buffer that does not divide it rounds up to cover the period");
		harness.expectEqual(wasapi::initialBridge(0, 256), 1u, "no buffer, no bridge");
		for (unsigned minPeriod : {0u, 96u, 144u, 288u, 4000u})
		{
			const wasapi::BufferPolicy offered = wasapi::bufferPolicy(minPeriod);
			for (long frames = offered.minSize; frames <= offered.preferredSize; frames *= 2)
			{
				const unsigned bridge = wasapi::initialBridge(frames, offered.preferredSize);
				harness.expectTrue(bridge <= wasapi::bridgeCap && static_cast<long>(bridge) * frames >= offered.preferredSize,
					"every offered size reaches the device minimum within the cap: " + std::to_string(frames) + " over " + std::to_string(minPeriod));
			}
		}

		// Frames and 100 ns units.
		harness.expectEqual(wasapi::framesFromHns(30000, 48000), 144u, "3 ms at 48 kHz is 144 frames");
		harness.expectEqual(wasapi::framesFromHns(100000, 44100), 441u, "10 ms at 44.1 kHz is 441 frames");
		harness.expectEqual(wasapi::framesFromHns(26667, 96000), 256u, "26667 hns at 96 kHz is 256 frames, to nearest");
		harness.expectEqual(wasapi::framesFromHns(30000, 44100), 132u, "3 ms at 44.1 kHz is 132 frames, to nearest");
		harness.expectEqual(wasapi::hnsFromFrames(144, 48000), 30000LL, "144 frames at 48 kHz is 3 ms");
		harness.expectEqual(wasapi::hnsFromFrames(441, 44100), 100000LL, "441 frames at 44.1 kHz is 10 ms");
		harness.expectEqual(wasapi::framesFromHns(wasapi::hnsFromFrames(256, 96000), 96000), 256u, "frames survive the round trip");

		// Capture packets retain only what fits while the whole packet is released.
		wasapi::CapturePlan capturePlan = wasapi::planCapturePacket(32, 128, 64);
		harness.expectEqual(capturePlan.dropFromQueue, static_cast<size_t>(0), "a fitting capture packet drops nothing");
		harness.expectEqual(capturePlan.copyFrames, static_cast<size_t>(64), "a fitting capture packet is copied whole");
		capturePlan = wasapi::planCapturePacket(96, 128, 64);
		harness.expectEqual(capturePlan.dropFromQueue, static_cast<size_t>(32), "capture overflow drops exactly the excess");
		harness.expectEqual(capturePlan.copyFrames, static_cast<size_t>(64), "capture overflow still copies the packet");
		capturePlan = wasapi::planCapturePacket(48, 128, 256);
		harness.expectEqual(capturePlan.dropFromQueue, static_cast<size_t>(48), "an oversized packet drops the queued frames");
		harness.expectEqual(capturePlan.copyFrames, static_cast<size_t>(128), "an oversized packet copies one capacity");

		// Interleaving, every container width.
		for (unsigned bytes : {2u, 3u, 4u})
		{
			const unsigned channels = 3, frames = 5;
			std::vector<std::vector<unsigned char>> planes(channels, std::vector<unsigned char>(static_cast<size_t>(frames) * bytes));
			for (unsigned c = 0; c < channels; c++)
				for (size_t i = 0; i < planes[c].size(); i++)
					planes[c][i] = static_cast<unsigned char>(c * 64 + i);
			std::vector<const void*> in(channels);
			// planes is a vector of vectors: planes[c] is an element, not an
			// offset from one object (cppcheck 2.22 false positive).
			for (unsigned c = 0; c < channels; c++)
				// cppcheck-suppress objectIndex
				in[c] = planes[c].data();
			std::vector<unsigned char> block(static_cast<size_t>(frames) * channels * bytes);
			wasapi::interleave(in.data(), channels, bytes, frames, block.data());
			bool laidOut = true;
			for (unsigned f = 0; f < frames; f++)
				for (unsigned c = 0; c < channels; c++)
					for (unsigned b = 0; b < bytes; b++)
						laidOut = laidOut && block[(static_cast<size_t>(f) * channels + c) * bytes + b] == static_cast<unsigned char>(c * 64 + f * bytes + b);
			harness.expectTrue(laidOut, "interleave lays frames out channel by channel at " + std::to_string(bytes) + " bytes");
			std::vector<std::vector<unsigned char>> back(channels, std::vector<unsigned char>(static_cast<size_t>(frames) * bytes));
			std::vector<void*> out(channels);
			for (unsigned c = 0; c < channels; c++)
				// cppcheck-suppress objectIndex
				out[c] = back[c].data();
			wasapi::deinterleave(block.data(), channels, bytes, frames, out.data());
			harness.expectTrue(back == planes, "deinterleave restores the planes at " + std::to_string(bytes) + " bytes");
		}

		harness.expectTrue(wasapi::endpointId(false, L"{abc}") == L"{0.0.0.00000000}.{abc}", "a playback endpoint id");
		harness.expectTrue(wasapi::endpointId(true, L"{abc}") == L"{0.0.1.00000000}.{abc}", "a recording endpoint id");

		// A target with no endpoint refuses init with a message, and answers
		// nothing else, without touching the device stack.
		eapo::asio::WasapiExclusiveTarget empty(L"", L"");
		harness.expectEqual(static_cast<int>(empty.init(nullptr)), static_cast<int>(ASIOFalse), "no endpoint: init fails");
		char message[124] = {};
		empty.getErrorMessage(message);
		harness.expectTrue(std::strlen(message) > 0, "and says why");
		long ins = 1, outs = 1;
		harness.expectEqual(empty.getChannels(&ins, &outs), static_cast<ASIOError>(ASE_NotPresent), "channels are not present before init");
		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		eapo::asio::WasapiExclusiveTarget missing(L"{00000000-0000-0000-0000-000000000000}", L"");
		harness.expectEqual(static_cast<int>(missing.init(nullptr)), static_cast<int>(ASIOFalse), "an endpoint that does not exist: init fails");
		missing.getErrorMessage(message);
		harness.expectTrue(std::strstr(message, "not present") != nullptr || std::strstr(message, "enumerator") != nullptr,
			std::string("naming the missing endpoint: ") + message);
		if (SUCCEEDED(com))
			CoUninitialize();
	}

	// The WASAPI target's stream decisions, taken out of the stream thread:
	// the bridge calibration, the capture queue, the output staging and the
	// judgement of a device that went away, fed synthetic numbers.
	TEST_CASE(AsioTests, testWasapiStreamParts)
	{
		namespace wasapi = eapo::asio::wasapi;
		const uint64_t period = 2666666;     // 128 frames at 48 kHz, in ns

		// Calibration: the median of twelve event spacings against the period.
		const auto calibrate = [](uint64_t periodNanos, int forced, const std::vector<uint64_t>& spacings, unsigned* decidedOn, unsigned current = 1)
		{
			wasapi::BridgeCalibrator calibrator(periodNanos, current, forced);
			*decidedOn = 0;
			for (size_t i = 0; i < spacings.size(); i++)
				if (calibrator.addSpacing(spacings[i]))
					*decidedOn = static_cast<unsigned>(i + 1);
			return calibrator;
		};
		unsigned decidedOn = 0;
		std::vector<uint64_t> spacings(wasapi::bridgeCalibrationEvents - 1, period);
		wasapi::BridgeCalibrator partial = calibrate(period, 0, spacings, &decidedOn);
		harness.expectFalse(partial.decided(), "eleven events do not decide the bridge");
		spacings.push_back(period);
		wasapi::BridgeCalibrator steady = calibrate(period, 0, spacings, &decidedOn);
		harness.expectTrue(steady.decided(), "the twelfth event decides");
		harness.expectEqual(decidedOn, wasapi::bridgeCalibrationEvents, "on the twelfth spacing, once");
		harness.expectEqual(steady.factor(), 1u, "steady spacing at the period keeps a bridge of 1");
		harness.expectFalse(steady.forced(), "a calibrated bridge is not forced");
		harness.expectFalse(steady.addSpacing(period * 10), "spacings after the decision are ignored");
		harness.expectEqual(steady.factor(), 1u, "and do not change it");

		const uint64_t threshold = period + period / 2;
		wasapi::BridgeCalibrator atThreshold = calibrate(period, 0, std::vector<uint64_t>(12, threshold), &decidedOn);
		harness.expectEqual(atThreshold.factor(), 1u, "a median of exactly 1.5 periods keeps 1");
		wasapi::BridgeCalibrator below = calibrate(period, 0, std::vector<uint64_t>(12, threshold - 1000), &decidedOn);
		harness.expectEqual(below.factor(), 1u, "a median just below 1.5 periods keeps 1");
		wasapi::BridgeCalibrator above = calibrate(period, 0, std::vector<uint64_t>(12, threshold + 1000), &decidedOn);
		harness.expectEqual(above.factor(), 2u, "a median just above 1.5 periods bridges 2, rounded up");
		// The virtual cable of docs/features/asio.md: 10 ms against a 5.8 ms period.
		wasapi::BridgeCalibrator cable = calibrate(5805000, 0, std::vector<uint64_t>(12, 10000000), &decidedOn);
		harness.expectEqual(cable.factor(), 2u, "a 10 ms cycle over a 5.8 ms period bridges 2");

		std::vector<uint64_t> outliers(12, period);
		outliers[3] = period * 5;
		outliers[9] = period * 7;
		wasapi::BridgeCalibrator stalls = calibrate(period, 0, outliers, &decidedOn);
		harness.expectEqual(stalls.factor(), 1u, "two slow outliers among twelve do not move the median");

		wasapi::BridgeCalibrator capped = calibrate(period, 0, std::vector<uint64_t>(12, period * 20), &decidedOn);
		harness.expectEqual(capped.factor(), wasapi::bridgeCap, "a cycle of twenty periods is capped at 8");
		wasapi::BridgeCalibrator unknownPeriod = calibrate(0, 0, std::vector<uint64_t>(12, period * 3), &decidedOn);
		harness.expectEqual(unknownPeriod.factor(), 1u, "no period, no bridge");

		wasapi::BridgeCalibrator forced = calibrate(period, 3, std::vector<uint64_t>(12, period), &decidedOn);
		harness.expectTrue(forced.forced() && forced.decided(), "a forced value decides up front");
		harness.expectEqual(forced.factor(), 3u, "and wins over steady spacing");
		harness.expectEqual(decidedOn, 0u, "no spacing decides after it");
		for (int ignored : {1, 0, -2, 9})
		{
			wasapi::BridgeCalibrator notForced(period, 1, ignored);
			harness.expectFalse(notForced.forced() || notForced.decided(), "a forced value of " + std::to_string(ignored) + " is ignored");
		}
		wasapi::BridgeCalibrator forcedCap(period, 1, 8);
		harness.expectEqual(forcedCap.factor(), 8u, "the cap itself may be forced");

		// A stream that opened with a bridge (a buffer below the device
		// minimum) is judged against its device period, and never asked
		// to go below it.
		wasapi::BridgeCalibrator opened(period, 2, 0);
		harness.expectEqual(opened.factor(), 2u, "before any spacing the factor is the bridge the stream opened with");
		wasapi::BridgeCalibrator honoured = calibrate(period, 0, std::vector<uint64_t>(12, period * 2), &decidedOn, 2);
		harness.expectEqual(honoured.factor(), 2u, "events at the two-buffer device period keep the bridge of 2");
		wasapi::BridgeCalibrator underThree = calibrate(period, 0, std::vector<uint64_t>(12, period * 3), &decidedOn, 2);
		harness.expectEqual(underThree.factor(), 2u, "a median of 1.5 device periods keeps it");
		wasapi::BridgeCalibrator slowCable = calibrate(1333333, 0, std::vector<uint64_t>(12, 10000000), &decidedOn, 2);
		harness.expectEqual(slowCable.factor(), 8u, "64 frames over a 10 ms cycle: from 2 up to 8");
		wasapi::BridgeCalibrator fastEvents = calibrate(period, 0, std::vector<uint64_t>(12, period), &decidedOn, 4);
		harness.expectEqual(fastEvents.factor(), 4u, "events faster than the device period never lower the bridge");
		// The CI runner's cable (a 441-frame cycle at 44.1 kHz) under a
		// 64-frame stream: opened with four buffers per period, its 10 ms
		// cycle asks for seven; at 448 frames it signalled every 20 ms, and
		// the stream thread judges the reopened period again.
		const uint64_t runnerPeriod = 1451247;     // 64 frames at 44.1 kHz, in ns
		wasapi::BridgeCalibrator runnerOpened = calibrate(runnerPeriod, 0, std::vector<uint64_t>(12, 10000000), &decidedOn, 4);
		harness.expectEqual(runnerOpened.factor(), 7u, "a 10 ms cycle over 64 frames opened at 4 asks for 7");
		wasapi::BridgeCalibrator runnerReopened = calibrate(runnerPeriod, 0, std::vector<uint64_t>(12, 20000000), &decidedOn, 7);
		harness.expectEqual(runnerReopened.factor(), 8u, "the reopened 448-frame period signalled every 20 ms goes to the cap");
		wasapi::BridgeCalibrator runnerSettled = calibrate(runnerPeriod, 0, std::vector<uint64_t>(12, 11610000), &decidedOn, 8);
		harness.expectEqual(runnerSettled.factor(), 8u, "at 512 frames the cable keeps pace and the bridge stays");
		wasapi::BridgeCalibrator runnerCapped = calibrate(runnerPeriod, 0, std::vector<uint64_t>(12, 20000000), &decidedOn, 8);
		harness.expectEqual(runnerCapped.factor(), 8u, "at the cap a slower cycle asks for nothing more");
		wasapi::BridgeCalibrator forcedBelow(period, 4, 3);
		harness.expectFalse(forcedBelow.forced(), "a forced value at or below the opened bridge is ignored");
		harness.expectEqual(forcedBelow.factor(), 4u, "and the opened bridge stands");
		wasapi::BridgeCalibrator forcedAbove(period, 2, 5);
		harness.expectTrue(forcedAbove.forced(), "a forced value above the opened bridge decides");
		harness.expectEqual(forcedAbove.factor(), 5u, "with its own value");

		// Capture queue: two channels of 16 bit, room for two periods of 4 frames.
		const unsigned channels = 2, bytes = 2, frames = 4;
		wasapi::CaptureQueue queue;
		queue.reset(channels, bytes, frames * 2);
		harness.expectEqual(queue.capacityFrames(), static_cast<size_t>(8), "capacity is counted in frames");
		std::vector<uint16_t> left(frames), right(frames);
		void* planes[2] = {left.data(), right.data()};
		const auto packet = [](uint16_t first, size_t count)
		{
			std::vector<uint16_t> interleaved(count * 2);
			for (size_t f = 0; f < count; f++)
			{
				interleaved[f * 2] = static_cast<uint16_t>(first + f);
				interleaved[f * 2 + 1] = static_cast<uint16_t>(0x100 + first + f);
			}
			return interleaved;
		};

		std::vector<uint16_t> three = packet(1, 3);
		queue.push(three.data(), 3, false);
		for (unsigned f = 0; f < frames; f++)
			left[f] = right[f] = 0xffff;
		harness.expectFalse(queue.take(planes, frames), "a partial packet is not yet a period");
		harness.expectTrue(left == std::vector<uint16_t>(frames, 0) && right == std::vector<uint16_t>(frames, 0), "the short period is zero-filled");
		harness.expectEqual(queue.underruns(), static_cast<uint64_t>(1), "and counted as an underrun");
		harness.expectEqual(queue.pendingFrames(), static_cast<size_t>(3), "the partial packet stays queued");
		std::vector<uint16_t> two = packet(4, 2);
		queue.push(two.data(), 2, false);
		harness.expectTrue(queue.take(planes, frames), "the next packet completes the period");
		harness.expectTrue(left == std::vector<uint16_t>({1, 2, 3, 4}) && right == std::vector<uint16_t>({0x101, 0x102, 0x103, 0x104}),
			"the period is deinterleaved across the two packets in order");
		harness.expectEqual(queue.pendingFrames(), static_cast<size_t>(1), "the frame past the period waits for the next one");
		harness.expectEqual(queue.underruns(), static_cast<uint64_t>(1), "a full period is no underrun");

		std::vector<uint16_t> garbage = packet(0x55, 3);
		queue.push(garbage.data(), 3, true);
		harness.expectTrue(queue.take(planes, frames), "a silent packet still counts its frames");
		harness.expectTrue(left == std::vector<uint16_t>({5, 0, 0, 0}) && right == std::vector<uint16_t>({0x105, 0, 0, 0}),
			"and queues zeros in place of its data");
		queue.push(nullptr, 4, true);
		harness.expectTrue(queue.take(planes, frames), "a silent packet needs no data");
		harness.expectTrue(left == std::vector<uint16_t>(frames, 0), "and is silence");

		std::vector<uint16_t> flood = packet(10, 10);
		queue.push(flood.data(), 10, false);
		harness.expectEqual(queue.pendingFrames(), static_cast<size_t>(8), "an oversized packet fills the queue to its capacity");
		queue.clear();
		harness.expectEqual(queue.pendingFrames(), static_cast<size_t>(0), "clear empties the queue");

		// Output staging: bridge 4 over 128-frame periods.
		wasapi::OutputStager stager;
		stager.reset(4, 128);
		unsigned writes = 0;
		bool slotsInOrder = true, writesOnFourth = true, writeSize = true;
		for (unsigned n = 0; n < 10; n++)
		{
			const wasapi::OutputStager::Step step = stager.stage();
			slotsInOrder = slotsInOrder && step.slot == n % 4;
			const bool due = step.writeFrames != 0;
			writesOnFourth = writesOnFourth && due == (n % 4 == 3);
			if (due)
			{
				writes++;
				writeSize = writeSize && step.writeFrames == 512;
			}
		}
		harness.expectTrue(slotsInOrder, "each period lands at the next slot of the device block");
		harness.expectTrue(writesOnFourth, "the device is written every fourth period");
		harness.expectTrue(writeSize, "with four periods' frames");
		harness.expectEqual(writes, 2u, "ten periods at bridge 4 make two writes");
		harness.expectEqual(stager.staged(), 2u, "and leave two periods staged");
		stager.reset(1, 128);
		const wasapi::OutputStager::Step single = stager.stage();
		harness.expectTrue(single.slot == 0 && single.writeFrames == 128, "bridge 1 writes every period");

		// Health: a vanished device from either device call, or four silent waits.
		wasapi::StreamHealthJudge judge;
		harness.expectTrue(judge.judgeWait(false, S_OK) == wasapi::StreamHealth::Continue, "an event with good calls continues");
		harness.expectTrue(judge.judgeWait(false, AUDCLNT_E_BUFFER_TOO_LARGE) == wasapi::StreamHealth::Continue, "another failure is a miss, not a lost device");
		harness.expectTrue(judge.judgeWait(false, AUDCLNT_E_DEVICE_INVALIDATED) == wasapi::StreamHealth::DeviceLost, "an invalidated device from GetBuffer or ReleaseBuffer is lost");
		wasapi::StreamHealthJudge waits;
		for (unsigned i = 1; i < wasapi::deviceLostTimeouts; i++)
			harness.expectTrue(waits.judgeWait(true, S_OK) == wasapi::StreamHealth::Retry, "timeout " + std::to_string(i) + " is retried");
		harness.expectTrue(waits.judgeWait(true, S_OK) == wasapi::StreamHealth::DeviceLost, "the fourth timeout in a row is a lost device");
		wasapi::StreamHealthJudge recovering;
		for (unsigned i = 1; i < wasapi::deviceLostTimeouts; i++)
			recovering.judgeWait(true, S_OK);
		harness.expectEqual(recovering.consecutiveTimeouts(), 3u, "three timeouts are counted");
		harness.expectTrue(recovering.judgeWait(false, S_OK) == wasapi::StreamHealth::Continue, "an event between them continues");
		harness.expectEqual(recovering.consecutiveTimeouts(), 0u, "and resets the count");
		harness.expectTrue(recovering.judgeWait(true, S_OK) == wasapi::StreamHealth::Retry, "so the next timeout is the first again");
	}

	TEST_CASE(AsioTests, testWasapiCapturePacketTimeline)
	{
		namespace wasapi = eapo::asio::wasapi;

		wasapi::CapturePacketTimeline contiguous;
		contiguous.add(100, 32, 0);
		contiguous.add(132, 16, 0);
		harness.expectEqual(contiguous.counts().missingFrames, 0ull, "contiguous capture packets have no missing frames");
		harness.expectEqual(contiguous.counts().repeatedFrames, 0ull, "contiguous capture packets have no repeated frames");

		wasapi::CapturePacketTimeline gap;
		gap.add(100, 20, 0);
		gap.add(130, 10, 0);
		harness.expectEqual(gap.counts().missingFrames, 10ull, "a device-position gap counts the missing capture frames");

		wasapi::CapturePacketTimeline overlap;
		overlap.add(100, 20, 0);
		overlap.add(112, 16, 0);
		harness.expectEqual(overlap.counts().repeatedFrames, 8ull, "an overlapping packet counts the repeated capture frames");

		wasapi::CapturePacketTimeline discontinuity;
		discontinuity.add(100, 20, 0);
		discontinuity.add(120, 20, AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY);
		harness.expectEqual(discontinuity.counts().discontinuities, 1ull, "the WASAPI discontinuity flag is counted");
		harness.expectEqual(discontinuity.counts().missingFrames, 0ull, "a contiguous discontinuity does not invent missing frames");
		harness.expectEqual(discontinuity.counts().repeatedFrames, 0ull, "a discontinuity does not invent repeated frames");
	}

	eapo::asio::wasapi::EndpointSetup fakeRenderSetup(std::unique_ptr<asiotest::FakeEndpointClient> client,
		unsigned minPeriodFrames)
	{
		eapo::asio::wasapi::EndpointSetup setup;
		setup.client = std::move(client);
		setup.friendlyName = L"Deterministic endpoint";
		setup.channels = 2;
		setup.channelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
		setup.sampleRate = 48000;
		setup.minPeriodHns = eapo::asio::wasapi::hnsFromFrames(minPeriodFrames, setup.sampleRate);
		setup.container = {ASIOSTFloat32LSB, 32, 32, true};
		return setup;
	}

	TEST_CASE(AsioTests, testWasapiEndpointClientCalibration)
	{
		asiotest::HostStub::Options hostOptions;
		hostOptions.sampleType = ASIOSTFloat32LSB;
		hostOptions.outputSeed = 0;

		{
			auto endpoint = std::make_unique<asiotest::FakeEndpointClient>(5);
			asiotest::FakeEndpointClient* observed = endpoint.get();
			eapo::asio::WasapiExclusiveTarget target(fakeRenderSetup(std::move(endpoint), 256), {});
			asiotest::HostStub host(hostOptions);
			host.openChannels(0, 2);
			harness.requireEqual(target.init(nullptr), ASIOTrue, "the cadence-controlled endpoint initializes");
			harness.requireEqual(host.createBuffers(&target, 256), ASE_OK, "its stream buffers open");
			harness.requireEqual(target.start(), ASE_OK, "its stream starts");
			const bool enoughEvents = waitUntil([&host] {return host.switches() >= 12;}, 5000);
			target.stop();
			const eapo::asio::WasapiExclusiveTarget::Counters counters = target.counters();
			harness.expect(enoughEvents, "the stream thread reaches the calibration event count before the deadline");
			harness.expectEqual(observed->initializeCalls(), 1u, "events at the requested cadence do not reopen the endpoint");
			harness.expectEqual(counters.bridge, 1ull, "calibration keeps a one-period bridge");
			harness.expect(counters.periods >= 12, "the stream thread observed enough events to calibrate");
			host.disposeBuffers();
		}

		{
			auto endpoint = std::make_unique<asiotest::FakeEndpointClient>(10);
			asiotest::FakeEndpointClient* observed = endpoint.get();
			eapo::asio::WasapiExclusiveTarget target(fakeRenderSetup(std::move(endpoint), 64), {});
			asiotest::HostStub host(hostOptions);
			host.openChannels(0, 2);
			harness.requireEqual(target.init(nullptr), ASIOTrue, "the slow endpoint initializes");
			harness.requireEqual(host.createBuffers(&target, 64), ASE_OK, "the slow endpoint opens at 64 frames");
			harness.requireEqual(target.start(), ASE_OK, "the slow endpoint starts");
			const bool recalibrated = waitUntil([observed, &host] {
				return observed->initializeCalls() >= 2 && host.latencyChanges() >= 1;
			}, 5000);
			target.stop();
			const eapo::asio::WasapiExclusiveTarget::Counters counters = target.counters();
			harness.expect(recalibrated, "the slow endpoint finishes recalibration before the deadline");
			harness.expect(observed->initializeCalls() >= 2, "calibration reopens a slow endpoint with a larger device period");
			harness.expect(counters.bridge > 1, "the reopened stream reports the calibrated bridge");
			harness.expect(host.latencyChanges() >= 1, "the host is told that rebridge changed latency");
			host.disposeBuffers();
		}
	}

	TEST_CASE(AsioTests, testWasapiEndpointClientDeviceLost)
	{
		auto endpoint = std::make_unique<asiotest::FakeEndpointClient>(2, 4);
		asiotest::FakeEndpointClient* observed = endpoint.get();
		eapo::asio::WasapiExclusiveTarget target(fakeRenderSetup(std::move(endpoint), 64), {});
		asiotest::HostStub::Options hostOptions;
		hostOptions.sampleType = ASIOSTFloat32LSB;
		hostOptions.outputSeed = 0;
		asiotest::HostStub host(hostOptions);
		host.openChannels(0, 2);
		harness.requireEqual(target.init(nullptr), ASIOTrue, "the device-loss endpoint initializes");
		harness.requireEqual(host.createBuffers(&target, 64), ASE_OK, "its stream buffers open");
		harness.requireEqual(target.start(), ASE_OK, "its stream starts");
		const bool resetRequested = waitUntil([observed, &host] {
			return observed->releaseCalls() >= 4 && host.resetRequests() >= 1;
		}, 5000);
		target.stop();
		harness.expect(resetRequested, "device loss requests a reset before the deadline");
		harness.expect(observed->releaseCalls() >= 4, "the fake invalidated a render release");
		harness.expectEqual(host.resetRequests(), 1ul, "device loss asks the ASIO host to reset once");
		char message[eapo::asio::errorMessageBytes] = {};
		target.getErrorMessage(message);
		harness.expect(std::strstr(message, "went away") != nullptr, std::string("the failure names the lost device: ") + message);
		host.disposeBuffers();
	}

	// Audit #348 TD-10: a Korean endpoint name, narrowed with the ANSI code
	// page by the WASAPI target, must widen back to the same text and never be
	// cut in the middle of a double-byte character. Pinned with code page 949
	// so the check does not depend on the runner's system locale.
	TEST_CASE(AsioTests, testDriverNameText)
	{
		if (!IsValidCodePage(949))
		{
			std::printf("DriverNameText: code page 949 not installed, skipped\n");
			return;
		}
		// "AB" + U+C2A4 U+D53C U+CEE4 in code page 949.
		const char korean[] = {'A', 'B', '\xBD', '\xBA', '\xC7', '\xC7', '\xC4', '\xBF', '\0'};
		wchar_t wide[16] = {};
		eapo::asio::drivername::widen(korean, wide, 16, 949);
		harness.expect(std::wstring(wide) == std::wstring(L"AB\uC2A4\uD53C\uCEE4"),
			"a code page 949 name widens to the same text");

		harness.expectEqual(eapo::asio::drivername::characterBoundary(korean, 8, 5, 949), static_cast<size_t>(4),
			"a cut at 5 bytes drops the lead byte of the second character");
		harness.expectEqual(eapo::asio::drivername::characterBoundary(korean, 8, 6, 949), static_cast<size_t>(6),
			"a cut on a boundary keeps it");
		const char utf8[] = {'A', '\xE2', '\x82', '\xAC', 'B', '\0'};
		harness.expectEqual(eapo::asio::drivername::characterBoundary(utf8, 5, 3, CP_UTF8), static_cast<size_t>(1),
			"a UTF-8 sequence is not split at the byte limit");
		harness.expectEqual(eapo::asio::drivername::characterBoundary(utf8, 5, 4, CP_UTF8), static_cast<size_t>(4),
			"a complete UTF-8 sequence is retained");
		harness.expectEqual(eapo::asio::drivername::characterBoundary(korean, 8, 31, 949), static_cast<size_t>(8),
			"a short name is kept whole");

		const char ascii[] = "TOPPING USB DAC";
		wchar_t asciiWide[8] = {};
		eapo::asio::drivername::widen(ascii, asciiWide, 8, 949);
		harness.expect(std::wstring(asciiWide) == std::wstring(L"TOPPING"),
			"widening truncates to the destination and terminates it");
	}

	TEST_CASE(AsioTests, testCodecBytePatterns)
	{
		SampleCodec codec;
		eapo::asio::findSampleCodec(ASIOSTInt16LSB, codec);
		const unsigned char maxLsb[2] = {0xff, 0x7f};
		double value = 0.0;
		codec.toDouble(maxLsb, &value, 1);
		harness.expectNear(value, 32767.0 / 32768.0, 1e-7, "Int16LSB 0x7fff is just under full scale");

		eapo::asio::findSampleCodec(ASIOSTInt16MSB, codec);
		const unsigned char maxMsb[2] = {0x80, 0x00};
		codec.toDouble(maxMsb, &value, 1);
		harness.expectNear(value, -1.0, 1e-7, "Int16MSB 0x8000 is negative full scale");

		eapo::asio::findSampleCodec(ASIOSTInt24LSB, codec);
		const unsigned char minus24[3] = {0x00, 0x00, 0x80};
		codec.toDouble(minus24, &value, 1);
		harness.expectNear(value, -1.0, 1e-7, "Int24LSB sign-extends from bit 23");

		eapo::asio::findSampleCodec(ASIOSTInt32LSB24, codec);
		const unsigned char low24[4] = {0xff, 0xff, 0x7f, 0x00};
		codec.toDouble(low24, &value, 1);
		harness.expectNear(value, 8388607.0 / 8388608.0, 1e-7, "Int32LSB24 keeps its 24 valid bits right-aligned");
		const unsigned char neg24[4] = {0x00, 0x00, 0x80, 0x00};
		codec.toDouble(neg24, &value, 1);
		harness.expectNear(value, -1.0, 1e-7, "Int32LSB24 sign-extends from bit 23");
		unsigned char written[4] = {};
		const double minusOne = -1.0;
		codec.fromDouble(&minusOne, written, 1);
		harness.expectEqual(static_cast<int>(written[3]), 0, "Int32LSB24 writes nothing above its 24 bits");

		eapo::asio::findSampleCodec(ASIOSTFloat32MSB, codec);
		const double one = 1.0;
		codec.fromDouble(&one, written, 1);
		harness.expectEqual(static_cast<int>(written[0]), 0x3f, "Float32MSB writes big-endian");
		harness.expectEqual(static_cast<int>(written[1]), 0x80, "Float32MSB writes big-endian (second byte)");
	}

	// ---- trampolines ----

	class CountingSink final : public eapo::asio::ITargetCallbackSink
	{
	public:
		unsigned switches = 0, rateChanges = 0, messages = 0;
		long lastIndex = -1;

		void onBufferSwitch(long index, ASIOBool) noexcept override
		{
			switches++;
			lastIndex = index;
		}

		ASIOTime* onBufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool) noexcept override
		{
			switches++;
			lastIndex = index;
			return params;
		}

		void onSampleRateDidChange(ASIOSampleRate) noexcept override
		{
			rateChanges++;
		}

		long onAsioMessage(long, long, void*, double*) noexcept override
		{
			messages++;
			return 42;
		}
	};

	TEST_CASE(AsioTests, testTrampolines)
	{
		using eapo::asio::CallbackTrampolines::claim;
		using eapo::asio::CallbackTrampolines::release;
		CountingSink sinks[eapo::asio::CallbackTrampolines::slotCount + 1];
		ASIOCallbacks* sets[eapo::asio::CallbackTrampolines::slotCount] = {};
		for (unsigned i = 0; i < eapo::asio::CallbackTrampolines::slotCount; i++)
		{
			sets[i] = claim(&sinks[i]);
			harness.require(sets[i] != nullptr, "slot " + std::to_string(i) + " can be claimed");
		}
		harness.expect(claim(&sinks[eapo::asio::CallbackTrampolines::slotCount]) == nullptr, "a fifth claim finds no slot");
		for (unsigned i = 0; i < eapo::asio::CallbackTrampolines::slotCount; i++)
		{
			for (unsigned j = 0; j < eapo::asio::CallbackTrampolines::slotCount; j++)
				harness.expect(i == j || sets[i] != sets[j], "slots hand out distinct callback sets");
		}
		sets[1]->bufferSwitch(1, ASIOTrue);
		sets[1]->sampleRateDidChange(44100.0);
		harness.expectEqual(sets[1]->asioMessage(kAsioResetRequest, 0, nullptr, nullptr), 42L, "asioMessage reaches the bound sink");
		harness.expectEqual(sinks[1].switches, 1u, "bufferSwitch reaches the bound sink");
		harness.expectEqual(sinks[1].lastIndex, 1L, "the switch index is forwarded");
		harness.expectEqual(sinks[0].switches, 0u, "other sinks are untouched");
		harness.expect(release(&sinks[1]), "a released slot with no entrants is drained");
		sets[1]->bufferSwitch(0, ASIOTrue);
		harness.expectEqual(sinks[1].switches, 1u, "a released slot drops callbacks");
		harness.expectEqual(sets[1]->asioMessage(kAsioResetRequest, 0, nullptr, nullptr), 0L, "a released slot answers 0");
		harness.expect(claim(&sinks[4]) == sets[1], "the released-and-drained slot is reused");

		using eapo::asio::CallbackTrampolines::testEnter;
		using eapo::asio::CallbackTrampolines::testLeave;
		harness.require(testEnter(sets[1]), "a callback can enter before release");
		harness.expectFalse(release(&sinks[4]), "release reports an entrant that has not drained");
		sets[1]->bufferSwitch(0, ASIOTrue);
		harness.expectEqual(sinks[4].switches, 0u, "a callback after CLOSED is dropped");
		harness.expect(claim(&sinks[1]) == nullptr, "a released-but-undrained slot is not reused");
		testLeave(sets[1]);
		harness.expect(claim(&sinks[1]) == sets[1], "a retired slot is reusable after its entrant drains");
		for (CountingSink& sink : sinks)
			release(&sink);
	}

	ASIOCallbacks* failedCreateEnteredCallback = nullptr;
	bool failedCreateCallbackEntered = false;

	void enterFailedCreateCallback(ASIOCallbacks* callbacks)
	{
		failedCreateEnteredCallback = callbacks;
		failedCreateCallbackEntered = eapo::asio::CallbackTrampolines::testEnter(callbacks);
	}

	// ---- wrapper: state machine ----

	TEST_CASE(AsioTests, testStateMachineOrdering)
	{
		FakeAsioConfig config;
		StreamOptions options;
		Rig rig(config, options, asiotest::HostStub::Options());
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(2, 2);

		harness.expectEqual(w->start(), ASE_InvalidMode, "start before init is refused");
		harness.expectEqual(rig.host->createBuffers(w, 64), ASE_InvalidMode, "createBuffers before init is refused");
		harness.expectEqual(w->init(nullptr), ASIOTrue, "init succeeds");
		harness.expectEqual(w->init(nullptr), ASIOTrue, "a second init is a yes");
		harness.expectEqual(rig.counters().initCalls, 1ul, "the target is initialized once");
		harness.expect(w->state() == AsioWrapper::State::Initialized, "state is Initialized after init");
		harness.expectEqual(w->start(), ASE_InvalidMode, "start before createBuffers is refused");
		harness.expectEqual(w->stop(), ASE_InvalidMode, "stop before start is refused");
		harness.expectEqual(w->disposeBuffers(), ASE_InvalidMode, "disposeBuffers without buffers is refused");
		harness.expectEqual(rig.counters().startCalls, 0ul, "refused calls never reach the target");

		harness.expectEqual(rig.host->createBuffers(w, 64), ASE_OK, "createBuffers succeeds");
		harness.expect(w->state() == AsioWrapper::State::Prepared, "state is Prepared");
		harness.expectEqual(rig.processor->opens, 1u, "the processor was opened once");
		harness.expectEqual(rig.processor->openedWith.frames, 64u, "the processor saw the buffer size");
		harness.expectEqual(rig.processor->openedWith.channels[0], 2u, "the processor saw every output channel");
		harness.expectEqual(rig.processor->openedWith.channels[1], 2u, "the processor saw every input channel");
		harness.expectNear(rig.processor->openedWith.sampleRate, 48000.0, 0.0, "the processor saw the rate");
		harness.expect(std::wstring(rig.processor->openedWith.deviceName) == L"FakeAsio", "the processor saw the target name");
		harness.expect(std::wstring(rig.processor->openedWith.deviceGuid) == testTargetClsid, "the processor saw the target CLSID");
		harness.expectEqual(rig.host->createBuffers(w, 64), ASE_InvalidMode, "a second createBuffers is refused");

		harness.expectEqual(w->start(), ASE_OK, "start succeeds");
		harness.expect(w->state() == AsioWrapper::State::Running, "state is Running");
		harness.expectEqual(w->start(), ASE_InvalidMode, "a second start is refused");
		harness.expectEqual(w->stop(), ASE_OK, "stop succeeds");
		harness.expect(w->state() == AsioWrapper::State::Prepared, "state is Prepared after stop");
		harness.expectEqual(w->disposeBuffers(), ASE_OK, "disposeBuffers succeeds");
		harness.expect(w->state() == AsioWrapper::State::Initialized, "state is Initialized after dispose");
		harness.expectEqual(rig.processor->closes, 1u, "the processor was closed once");
		harness.expectEqual(rig.counters().disposeBuffersCalls, 1ul, "the target's buffers were disposed");

		harness.expectEqual(rig.host->createBuffers(w, 128), ASE_OK, "buffers can be created again");
		harness.expectEqual(rig.processor->openedWith.frames, 128u, "the second open sees the new size");
		harness.expectEqual(w->start(), ASE_OK, "start after re-create");
		harness.expectEqual(w->disposeBuffers(), ASE_OK, "disposeBuffers while running stops first");
		harness.expectEqual(rig.counters().stopCalls, 2ul, "the target was stopped by the implicit stop");

		char name[32] = {};
		w->getDriverName(name);
		harness.expect(std::string(name) == "FakeAsio (EQ APO XT)", std::string("the driver name carries the suffix: ") + name);
	}

	TEST_CASE(AsioTests, testQuarantinedCallbackDetachesHostArrayAndDefersTargetDispose)
	{
		FakeAsioConfig config;
		StreamOptions options;
		Rig rig(config, options, asiotest::HostStub::Options());
		std::vector<ASIOBufferInfo> oldInfos = outputInfos(2);
		std::vector<ASIOBufferInfo> newInfos = outputInfos(2);
		harness.requireEqual(rig.wrapper->init(nullptr), ASIOTrue, "the quarantine fixture initializes");
		harness.requireEqual(rig.wrapper->createBuffers(oldInfos.data(), static_cast<long>(oldInfos.size()), 64, rig.host->callbacks()),
			ASE_OK, "the quarantine fixture opens buffers");
		ASIOCallbacks* callbacks = rig.fake->callbackSetForTesting();
		harness.require(eapo::asio::CallbackTrampolines::testEnter(callbacks), "a target callback is held in flight");
		const MMRESULT timer = timeBeginPeriod(1);
		harness.expectEqual(rig.wrapper->disposeBuffers(), ASE_OK, "dispose returns after the bounded quarantine wait");
		if (timer == TIMERR_NOERROR)
			timeEndPeriod(1);
		harness.expect(oldInfos[0].buffers[0] == nullptr && oldInfos[0].buffers[1] == nullptr,
			"dispose returns the old host buffer pointers before the array can expire");
		harness.expectEqual(rig.counters().disposeBuffersCalls, 0ul, "the target buffers remain alive while its callback is in flight");
		harness.expectEqual(rig.processor->closes, 0u, "the processor remains open while its callback is in flight");
		std::memset(oldInfos.data(), 0xA5, oldInfos.size() * sizeof(ASIOBufferInfo));
		const std::vector<unsigned char> sentinel(oldInfos.size() * sizeof(ASIOBufferInfo), 0xA5);
		harness.expectEqual(rig.wrapper->createBuffers(newInfos.data(), static_cast<long>(newInfos.size()), 64, rig.host->callbacks()),
			ASE_InvalidMode, "createBuffers refuses to overwrite the host callback snapshot while the old callback is quarantined");
		eapo::asio::CallbackTrampolines::testLeave(callbacks);
		harness.expectEqual(rig.wrapper->createBuffers(newInfos.data(), static_cast<long>(newInfos.size()), 64, rig.host->callbacks()),
			ASE_OK, "the drained quarantine permits a fresh callback snapshot");
		harness.expectEqual(rig.counters().disposeBuffersCalls, 1ul, "the old target buffers are disposed only after the callback drains");
		harness.expectEqual(rig.processor->closes, 1u, "the old processor closes only after the callback drains");
		harness.expect(std::memcmp(oldInfos.data(), sentinel.data(), sentinel.size()) == 0,
			"releasing the quarantine never writes through the expired host array");
		harness.expectEqual(rig.wrapper->disposeBuffers(), ASE_OK, "the reopened stream disposes normally");
	}

	TEST_CASE(AsioTests, testFailedProcessorOpenQuarantineDetachesHostArray)
	{
		FakeAsioConfig config;
		StreamOptions options;
		Rig rig(config, options, asiotest::HostStub::Options());
		std::vector<ASIOBufferInfo> oldInfos = outputInfos(2);
		std::vector<ASIOBufferInfo> newInfos = outputInfos(2);
		rig.processor->openStatus = OpenReport::Status::Unavailable;
		rig.processor->enterCallbackFrom = rig.fake;
		harness.requireEqual(rig.wrapper->init(nullptr), ASIOTrue, "the failed-open quarantine fixture initializes");
		const MMRESULT timer = timeBeginPeriod(1);
		harness.expectEqual(rig.wrapper->createBuffers(oldInfos.data(), static_cast<long>(oldInfos.size()), 64, rig.host->callbacks()),
			ASE_HWMalfunction, "a processor failure returns after the bounded quarantine wait");
		if (timer == TIMERR_NOERROR)
			timeEndPeriod(1);
		harness.require(rig.processor->callbackEntered, "the processor failure left a target callback in flight");
		harness.expect(oldInfos[0].buffers[0] == nullptr && oldInfos[0].buffers[1] == nullptr,
			"the failed create returns the old host buffer pointers before the array can expire");
		harness.expectEqual(rig.counters().disposeBuffersCalls, 0ul, "failed-create target buffers remain alive while the callback is in flight");
		harness.expectEqual(rig.processor->closes, 0u, "a partially opened processor remains alive while the callback is in flight");
		std::memset(oldInfos.data(), 0xA5, oldInfos.size() * sizeof(ASIOBufferInfo));
		const std::vector<unsigned char> sentinel(oldInfos.size() * sizeof(ASIOBufferInfo), 0xA5);
		harness.expectEqual(rig.wrapper->createBuffers(newInfos.data(), static_cast<long>(newInfos.size()), 64, rig.host->callbacks()),
			ASE_InvalidMode, "the failed create cannot replace callbacks before the old one drains");
		eapo::asio::CallbackTrampolines::testLeave(rig.processor->enteredCallback);
		rig.processor->enterCallbackFrom = nullptr;
		rig.processor->openStatus = OpenReport::Status::Ok;
		harness.expectEqual(rig.wrapper->createBuffers(newInfos.data(), static_cast<long>(newInfos.size()), 64, rig.host->callbacks()),
			ASE_OK, "the failed create can reopen after its callback drains");
		harness.expectEqual(rig.counters().disposeBuffersCalls, 1ul, "the failed create disposes target buffers after the callback drains");
		harness.expectEqual(rig.processor->closes, 1u, "the partial processor open closes after the callback drains");
		harness.expect(std::memcmp(oldInfos.data(), sentinel.data(), sentinel.size()) == 0,
			"the failed-create cleanup never writes through the expired host array");
		harness.expectEqual(rig.wrapper->disposeBuffers(), ASE_OK, "the recovered stream disposes normally");
	}

	TEST_CASE(AsioTests, testFailedTargetCreateQuarantinesNotificationCallback)
	{
		FakeAsioConfig config;
		config.failCreateAfterCallbacks = 1;
		StreamOptions options;
		Rig rig(config, options, asiotest::HostStub::Options());
		std::vector<ASIOBufferInfo> oldInfos = outputInfos(2);
		std::vector<ASIOBufferInfo> newInfos = outputInfos(2);
		failedCreateEnteredCallback = nullptr;
		failedCreateCallbackEntered = false;
		rig.fake->setCreateBuffersHookForTesting(&enterFailedCreateCallback);
		harness.requireEqual(rig.wrapper->init(nullptr), ASIOTrue, "the target-failure quarantine fixture initializes");
		const MMRESULT timer = timeBeginPeriod(1);
		harness.expectEqual(rig.wrapper->createBuffers(oldInfos.data(), static_cast<long>(oldInfos.size()), 64, rig.host->callbacks()),
			ASE_HWMalfunction, "a target failure returns after the bounded notification quarantine wait");
		if (timer == TIMERR_NOERROR)
			timeEndPeriod(1);
		harness.require(failedCreateCallbackEntered, "the target started a notification callback before returning failure");
		harness.expectEqual(rig.processor->opens, 0u, "the processor never opens after the target create fails");
		std::memset(oldInfos.data(), 0xA5, oldInfos.size() * sizeof(ASIOBufferInfo));
		const std::vector<unsigned char> sentinel(oldInfos.size() * sizeof(ASIOBufferInfo), 0xA5);
		harness.expectEqual(rig.wrapper->createBuffers(newInfos.data(), static_cast<long>(newInfos.size()), 64, rig.host->callbacks()),
			ASE_InvalidMode, "a target failure cannot replace the host snapshot before its notification drains");
		eapo::asio::CallbackTrampolines::testLeave(failedCreateEnteredCallback);
		rig.fake->setCreateBuffersHookForTesting(nullptr);
		config.failCreateAfterCallbacks = 0;
		harness.requireEqual(rig.fake->configure(&config), S_OK, "the fake target is allowed to recover");
		harness.expectEqual(rig.wrapper->createBuffers(newInfos.data(), static_cast<long>(newInfos.size()), 64, rig.host->callbacks()),
			ASE_OK, "the target can reopen after its notification drains");
		harness.expectEqual(rig.processor->closes, 0u, "releasing a failed target create does not close an unopened processor");
		harness.expect(std::memcmp(oldInfos.data(), sentinel.data(), sentinel.size()) == 0,
			"target-failure cleanup never writes through the expired host array");
		harness.expectEqual(rig.wrapper->disposeBuffers(), ASE_OK, "the recovered target stream disposes normally");
		failedCreateEnteredCallback = nullptr;
		failedCreateCallbackEntered = false;
	}

	// ---- wrapper: data flow ----

	TEST_CASE(AsioTests, testFloat64SamplesAreReportedForEveryChannel)
	{
		FakeAsioConfig config;
		config.inputChannels = 2;
		config.outputChannels = 3;
		config.sampleType = ASIOSTInt24LSB;
		StreamOptions options;
		options.float64Samples = true;
		Rig rig(config, options, asiotest::HostStub::Options());
		AsioWrapper* wrapper = rig.wrapper;
		harness.requireEqual(wrapper->init(nullptr), ASIOTrue, "initialize the Float64-reporting wrapper");

		const auto checkChannels = [&](const std::string& state) {
			for (int direction = 0; direction < 2; direction++)
			{
				const bool input = direction != 0;
				const long count = input ? config.inputChannels : config.outputChannels;
				for (long channel = 0; channel < count; channel++)
				{
					ASIOChannelInfo targetInfo = {};
					targetInfo.channel = channel;
					targetInfo.isInput = input ? ASIOTrue : ASIOFalse;
					harness.requireEqual(rig.fake->getChannelInfo(&targetInfo), ASE_OK,
						state + " target channel info");
					ASIOChannelInfo hostInfo = {};
					hostInfo.channel = channel;
					hostInfo.isInput = input ? ASIOTrue : ASIOFalse;
					harness.requireEqual(wrapper->getChannelInfo(&hostInfo), ASE_OK,
						state + " wrapper channel info");
					harness.expectEqual(hostInfo.type, static_cast<long>(ASIOSTFloat64LSB),
						state + " reports Float64LSB");
					harness.expectEqual(hostInfo.isActive, targetInfo.isActive,
						state + " keeps the target active flag");
					harness.expectEqual(hostInfo.channelGroup, targetInfo.channelGroup,
						state + " keeps the target channel group");
					harness.expect(std::strcmp(hostInfo.name, targetInfo.name) == 0,
						state + " keeps the target channel name");
				}
			}
		};

		checkChannels("before createBuffers");
		harness.expectEqual(wrapper->getChannelInfo(nullptr), ASE_InvalidParameter,
			"a null channel-info pointer is forwarded to the target");
		rig.host->openChannels(config.inputChannels, config.outputChannels);
		harness.requireEqual(rig.host->createBuffers(wrapper, 16), ASE_OK, "prepare Float64-reporting buffers");
		checkChannels("while prepared");
		harness.requireEqual(wrapper->start(), ASE_OK, "start Float64-reporting buffers");
		checkChannels("while running");
		harness.requireEqual(wrapper->stop(), ASE_OK, "stop Float64-reporting buffers");
		harness.requireEqual(wrapper->disposeBuffers(), ASE_OK, "dispose Float64-reporting buffers");
		checkChannels("after disposeBuffers");

		StreamOptions targetTypeOptions;
		Rig targetTypeRig(config, targetTypeOptions, asiotest::HostStub::Options());
		for (int direction = 0; direction < 2; direction++)
		{
			const bool input = direction != 0;
			const long count = input ? config.inputChannels : config.outputChannels;
			for (long channel = 0; channel < count; channel++)
			{
				ASIOChannelInfo info = {};
				info.channel = channel;
				info.isInput = input ? ASIOTrue : ASIOFalse;
				harness.requireEqual(targetTypeRig.wrapper->getChannelInfo(&info), ASE_OK,
					"default wrapper channel info");
				harness.expectEqual(info.type, static_cast<long>(ASIOSTInt24LSB),
					"without the option the application sees Int24LSB");
			}
		}
	}

	TEST_CASE(AsioTests, testFloat64SamplesReachTheTargetProcessed)
	{
		constexpr long testFrames = 16;
		constexpr long testPeriods = 3;
		FakeAsioConfig config;
		config.sampleType = ASIOSTInt32LSB;
		config.seed = 11;
		config.fullPrecision = 1;
		StreamOptions options;
		options.float64Samples = true;
		asiotest::HostStub::Options hostOptions;
		hostOptions.outputSeed = 7;
		hostOptions.fullPrecision = true;
		Rig rig(config, options, hostOptions);
		rig.processor->gain = 0.5f;
		rig.host->openChannels(2, 2);
		harness.requireEqual(rig.wrapper->init(nullptr), ASIOTrue, "initialize the converted processed stream");
		harness.requireEqual(rig.host->createBuffers(rig.wrapper, testFrames), ASE_OK,
			"create the converted processed stream");
		harness.requireEqual(rig.wrapper->start(), ASE_OK, "start the converted processed stream");
		harness.requireEqual(rig.control()->pump(testPeriods), S_OK, "pump the converted processed stream");
		harness.requireEqual(rig.wrapper->stop(), ASE_OK, "stop the converted processed stream");
		harness.requireEqual(rig.wrapper->disposeBuffers(), ASE_OK, "dispose the converted processed stream");

		for (long channel = 0; channel < 2; channel++)
		{
			std::vector<double> expectedOutput(static_cast<size_t>(testFrames * testPeriods));
			for (size_t sample = 0; sample < expectedOutput.size(); sample++)
				expectedOutput[sample] = 0.5 * rig.host->outputValue(channel, sample);
			expectExactBytes(captured(rig.control(), channel), oracleEncoded(ASIOSTInt32LSB, expectedOutput),
				"processed Float64 output reaches Int32 target channel " + std::to_string(channel));

			std::vector<double> expectedInput = oracleDecoded(ASIOSTInt32LSB, supplied(rig.control(), channel));
			for (double& sample : expectedInput)
				sample *= 0.5;
			expectExactBytes(rig.host->inputRecord(static_cast<size_t>(channel)),
				oracleEncoded(ASIOSTFloat64LSB, expectedInput),
				"processed Int32 input reaches Float64 host channel " + std::to_string(channel));
		}
	}

	TEST_CASE(AsioTests, testFloat64SamplesFallBackConverted)
	{
		constexpr long testFrames = 16;
		FakeAsioConfig config;
		config.inputChannels = 1;
		config.outputChannels = 1;
		config.sampleType = ASIOSTInt24LSB;
		config.seed = 11;
		config.fullPrecision = 1;
		StreamOptions options;
		options.processInput = false;
		options.float64Samples = true;
		asiotest::HostStub::Options hostOptions;
		hostOptions.outputSeed = 7;
		hostOptions.sampleType = ASIOSTFloat64LSB;
		hostOptions.fullPrecision = true;
		Rig rig(config, options, hostOptions);
		rig.processor->gain = 0.5f;
		rig.processor->script = {Outcome::Processed, Outcome::LateDropped, Outcome::Processed};
		rig.host->openChannels(1, 1);
		harness.requireEqual(rig.wrapper->init(nullptr), ASIOTrue, "initialize the converted fallback stream");
		harness.requireEqual(rig.host->createBuffers(rig.wrapper, testFrames), ASE_OK,
			"create the converted fallback stream");
		harness.requireEqual(rig.wrapper->start(), ASE_OK, "start the converted fallback stream");
		harness.requireEqual(rig.control()->pump(3), S_OK, "pump the converted fallback stream");
		harness.requireEqual(rig.wrapper->stop(), ASE_OK, "stop the converted fallback stream");
		harness.requireEqual(rig.wrapper->disposeBuffers(), ASE_OK, "dispose the converted fallback stream");

		std::vector<double> expectedOutput(static_cast<size_t>(testFrames * 3));
		for (size_t sample = 0; sample < expectedOutput.size(); sample++)
		{
			const size_t period = sample / static_cast<size_t>(testFrames);
			const double gain = period == 1 ? 1.0 : 0.5;
			expectedOutput[sample] = gain * rig.host->outputValue(0, sample);
		}
		expectExactBytes(captured(rig.control(), 0), oracleEncoded(ASIOSTInt24LSB, expectedOutput),
			"late Float64 output is converted without processing between processed periods");
		std::vector<double> expectedInput = oracleDecoded(ASIOSTInt24LSB, supplied(rig.control(), 0));
		expectExactBytes(rig.host->inputRecord(0), oracleEncoded(ASIOSTFloat64LSB, expectedInput),
			"disabled Int24 input is converted to Float64");

		FakeAsioDriver* fake = new FakeAsioDriver();
		harness.requireEqual(fake->configure(&config), S_OK, "configure the passthrough conversion target");
		StreamOptions passthroughOptions;
		passthroughOptions.float64Samples = true;
		AsioWrapper* wrapper = new AsioWrapper(fake, testWrapperClsid, testTargetClsid,
			passthroughOptions, std::make_unique<PassthroughProcessor>());
		fake->Release();
		std::vector<unsigned char> passthroughOutput;
		std::vector<unsigned char> passthroughInput;
		std::vector<unsigned char> passthroughSupplied;
		{
			asiotest::HostStub host(hostOptions);
			host.openChannels(1, 1);
			harness.requireEqual(wrapper->init(nullptr), ASIOTrue, "initialize the passthrough conversion stream");
			harness.requireEqual(host.createBuffers(wrapper, testFrames), ASE_OK, "create the passthrough conversion stream");
			harness.requireEqual(wrapper->start(), ASE_OK, "start the passthrough conversion stream");
			harness.requireEqual(fake->pump(2), S_OK, "pump the passthrough conversion stream");
			harness.requireEqual(wrapper->stop(), ASE_OK, "stop the passthrough conversion stream");
			harness.requireEqual(wrapper->disposeBuffers(), ASE_OK, "dispose the passthrough conversion stream");
			passthroughOutput = captured(fake, 0);
			passthroughInput = host.inputRecord(0);
			passthroughSupplied = supplied(fake, 0);
			std::vector<double> passthroughValues(static_cast<size_t>(testFrames * 2));
			for (size_t sample = 0; sample < passthroughValues.size(); sample++)
				passthroughValues[sample] = host.outputValue(0, sample);
			expectExactBytes(passthroughOutput, oracleEncoded(ASIOSTInt24LSB, passthroughValues),
				"passthrough Float64 output is converted to Int24");
		}
		expectExactBytes(passthroughInput,
			oracleEncoded(ASIOSTFloat64LSB, oracleDecoded(ASIOSTInt24LSB, passthroughSupplied)),
			"passthrough Int24 input is converted to Float64");
		wrapper->Release();
	}

	TEST_CASE(AsioTests, testFloat64SamplesUnopenedChannelsSeeSilence)
	{
		FakeAsioConfig config;
		config.outputChannels = 4;
		StreamOptions options;
		options.processInput = false;
		options.float64Samples = true;
		asiotest::HostStub::Options hostOptions;
		hostOptions.fullPrecision = true;
		Rig rig(config, options, hostOptions);
		rig.processor->gain = 1.0f;
		rig.host->openChannelList({}, {0, 2});
		harness.requireEqual(rig.wrapper->init(nullptr), ASIOTrue, "initialize Float64 unopened-channel stream");
		harness.requireEqual(rig.host->createBuffers(rig.wrapper, 16), ASE_OK,
			"create Float64 unopened-channel stream");
		harness.requireEqual(rig.wrapper->start(), ASE_OK, "start Float64 unopened-channel stream");
		harness.requireEqual(rig.control()->pump(2), S_OK, "pump Float64 unopened-channel stream");
		harness.requireEqual(rig.wrapper->stop(), ASE_OK, "stop Float64 unopened-channel stream");
		harness.requireEqual(rig.wrapper->disposeBuffers(), ASE_OK, "dispose Float64 unopened-channel stream");
		std::vector<double> unopened = oracleDecoded(config.sampleType, captured(rig.control(), 1));
		harness.expect(std::all_of(unopened.begin(), unopened.end(), [](double sample) {return sample == 0.0;}),
			"an unopened target channel is silent with Float64 host buffers");
		std::vector<double> opened = oracleDecoded(config.sampleType, captured(rig.control(), 2));
		harness.expect(std::any_of(opened.begin(), opened.end(), [](double sample) {return sample != 0.0;}),
			"an opened target channel carries its Float64 host signal");
	}

	TEST_CASE(AsioTests, testFloat64SamplesLeaveDsdAlone)
	{
		FakeAsioConfig config;
		config.sampleType = ASIOSTDSDInt8LSB1;
		StreamOptions options;
		options.float64Samples = true;
		Rig rig(config, options, asiotest::HostStub::Options());
		ASIOChannelInfo info = {};
		info.channel = 0;
		info.isInput = ASIOFalse;
		harness.requireEqual(rig.wrapper->getChannelInfo(&info), ASE_OK, "read a DSD channel with the Float64 option");
		harness.expectEqual(info.type, static_cast<long>(ASIOSTDSDInt8LSB1), "DSD is reported unchanged");
		rig.host->openChannels(1, 1);
		harness.requireEqual(rig.wrapper->init(nullptr), ASIOTrue, "initialize the DSD wrapper");
		harness.expectEqual(rig.host->createBuffers(rig.wrapper, 64), ASE_InvalidMode,
			"DSD is still refused at createBuffers");
	}

	TEST_CASE(AsioTests, testOutputAndInputAreProcessed)
	{
		FakeAsioConfig config;
		config.seed = 11;
		StreamOptions options;
		asiotest::HostStub::Options hostOptions;
		hostOptions.outputSeed = 7;
		Rig rig(config, options, hostOptions);
		rig.processor->gain = 0.5f;
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(2, 2);
		harness.require(w->init(nullptr) == ASIOTrue, "init");
		harness.require(rig.host->createBuffers(w, 64) == ASE_OK, "createBuffers");
		harness.require(w->start() == ASE_OK, "start");
		rig.control()->pump(4);
		w->stop();
		w->disposeBuffers();

		SampleCodec codec;
		eapo::asio::findSampleCodec(config.sampleType, codec);
		for (long c = 0; c < 2; c++)
		{
			std::vector<float> out = decode(codec, captured(rig.control(), c));
			harness.requireEqual(out.size(), static_cast<size_t>(4 * 64), "four periods reached output " + std::to_string(c));
			for (size_t n = 0; n < out.size(); n++)
			{
				const float expected = quantized(codec, rig.host->outputSample(c, n)) * 0.5f;
				if (std::fabs(out[n] - expected) > 1e-6f)
				{
					harness.expect(false, "output " + std::to_string(c) + " sample " + std::to_string(n) + " is the host signal at half gain");
					break;
				}
			}
			std::vector<float> in = decode(codec, rig.host->inputRecord(static_cast<size_t>(c)));
			harness.requireEqual(in.size(), static_cast<size_t>(4 * 64), "four periods reached the host from input " + std::to_string(c));
			for (size_t n = 0; n < in.size(); n++)
			{
				const float expected = quantized(codec, FakeAsioDriver::generatorSample(config.seed, c, n)) * 0.5f;
				if (std::fabs(in[n] - expected) > 1e-6f)
				{
					harness.expect(false, "input " + std::to_string(c) + " sample " + std::to_string(n) + " is the target signal at half gain");
					break;
				}
			}
		}
		harness.expectEqual(rig.processor->calls, 8u, "process ran once per direction per period");
		harness.expectEqual(rig.wrapper->stats().blocks[0], 4ull, "output blocks were counted");
		harness.expectEqual(rig.wrapper->stats().blocks[1], 4ull, "input blocks were counted");
		harness.expectEqual(rig.wrapper->stats().late[0] + rig.wrapper->stats().late[1], 0ull, "nothing was late");
		harness.expectEqual(rig.counters().timeInfoSwitches, 4ul, "the target used the time-info switch");
		harness.expectEqual(rig.host->timeInfoSwitches(), 4ul, "the host received the time-info switch");
	}

	TEST_CASE(AsioTests, testFirstBlockIsAlreadyProcessed)
	{
		FakeAsioConfig config;
		StreamOptions options;
		Rig rig(config, options, asiotest::HostStub::Options());
		rig.processor->gain = 0.25f;
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(0, 1);
		w->init(nullptr);
		rig.host->createBuffers(w, 32);
		w->start();
		rig.control()->pump(1);
		w->stop();
		w->disposeBuffers();

		SampleCodec codec;
		eapo::asio::findSampleCodec(config.sampleType, codec);
		std::vector<float> out = decode(codec, captured(rig.control(), 0));
		harness.requireEqual(out.size(), static_cast<size_t>(32), "one period was captured");
		bool allProcessed = true;
		for (size_t n = 0; n < out.size(); n++)
		{
			const float expected = quantized(codec, rig.host->outputSample(0, n)) * 0.25f;
			if (std::fabs(out[n] - expected) > 1e-6f)
				allProcessed = false;
		}
		harness.expect(allProcessed, "the very first block leaves the wrapper processed");
	}

	TEST_CASE(AsioTests, testLateAndOffPassThrough)
	{
		FakeAsioConfig config;
		StreamOptions options;
		options.processInput = false;
		Rig rig(config, options, asiotest::HostStub::Options());
		rig.processor->gain = 0.5f;
		rig.processor->script = {Outcome::Processed, Outcome::LateDropped, Outcome::Processed};
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(1, 1);
		w->init(nullptr);
		rig.host->createBuffers(w, 16);
		harness.expectEqual(rig.processor->openedWith.channels[1], 0u, "a disabled input direction opens with zero channels");
		w->start();
		rig.control()->pump(3);
		w->stop();
		w->disposeBuffers();

		SampleCodec codec;
		eapo::asio::findSampleCodec(config.sampleType, codec);
		std::vector<float> out = decode(codec, captured(rig.control(), 0));
		harness.requireEqual(out.size(), static_cast<size_t>(48), "three periods were captured");
		bool period0 = true, period1 = true, period2 = true;
		for (size_t n = 0; n < 16; n++)
		{
			const float raw = quantized(codec, rig.host->outputSample(0, n));
			const float raw1 = quantized(codec, rig.host->outputSample(0, 16 + n));
			const float raw2 = quantized(codec, rig.host->outputSample(0, 32 + n));
			period0 = period0 && std::fabs(out[n] - raw * 0.5f) < 1e-6f;
			period1 = period1 && std::fabs(out[16 + n] - raw1) < 1e-6f;
			period2 = period2 && std::fabs(out[32 + n] - raw2 * 0.5f) < 1e-6f;
		}
		harness.expect(period0, "period 0 was processed");
		harness.expect(period1, "the late period passed through untouched");
		harness.expect(period2, "processing resumed after the late period");
		harness.expectEqual(rig.wrapper->stats().late[0], 1ull, "the late block was counted");
		harness.expectEqual(rig.wrapper->stats().blocks[1], 0ull, "the disabled input direction was never processed");

		std::vector<float> in = decode(codec, rig.host->inputRecord(0));
		bool inputRaw = true;
		for (size_t n = 0; n < in.size(); n++)
			inputRaw = inputRaw && std::fabs(in[n] - quantized(codec, FakeAsioDriver::generatorSample(config.seed, 0, n))) < 1e-6f;
		harness.expect(inputRaw, "a disabled input direction is copied byte for byte");
	}

	TEST_CASE(AsioTests, testGoneIsStickyUntilReopen)
	{
		FakeAsioConfig config;
		StreamOptions options;
		options.processInput = false;
		Rig rig(config, options, asiotest::HostStub::Options());
		rig.processor->script = {Outcome::Gone};
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(0, 1);
		w->init(nullptr);
		rig.host->createBuffers(w, 16);
		w->start();
		rig.control()->pump(3);
		harness.expectEqual(rig.processor->calls, 1u, "after Gone the processor is not called again");
		harness.expectEqual(w->stop(), ASE_OK, "stop after Gone");
		harness.expectEqual(w->start(), ASE_HWMalfunction, "start after Gone fails loudly without a reopen");
		char message[124] = {};
		w->getErrorMessage(message);
		harness.expect(std::strstr(message, "went away") != nullptr, std::string("the error names the lost host: ") + message);
		harness.expectEqual(w->disposeBuffers(), ASE_OK, "dispose after Gone");
		harness.expectEqual(rig.wrapper->stats().gone[0], 1ull, "the Gone block was counted");
		rig.processor->script.clear();
		harness.expectEqual(rig.host->createBuffers(w, 16), ASE_OK, "reopen after Gone");
		harness.expectEqual(w->start(), ASE_OK, "start after the reopen works again");
		w->stop();
		w->disposeBuffers();
	}

	// Audit #348 TD-73: a reopened fake driver starts like a fresh one. Its
	// input generator used to carry on from where the previous stream
	// stopped, so a test that reopened could not predict the input from
	// sample 0.
	TEST_CASE(AsioTests, testReopenedTargetRestartsItsInput)
	{
		FakeAsioConfig config;
		config.seed = 5;
		StreamOptions options;
		options.processInput = false;
		Rig rig(config, options, asiotest::HostStub::Options());
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(1, 1);
		harness.require(w->init(nullptr) == ASIOTrue, "init");
		harness.require(rig.host->createBuffers(w, 16) == ASE_OK, "first createBuffers");
		harness.require(w->start() == ASE_OK, "first start");
		rig.control()->pump(3);
		w->stop();
		w->disposeBuffers();
		rig.control()->clearRecords();

		harness.require(rig.host->createBuffers(w, 16) == ASE_OK, "createBuffers again");
		harness.require(w->start() == ASE_OK, "start again");
		rig.control()->pump(2);
		w->stop();
		w->disposeBuffers();

		SampleCodec codec;
		eapo::asio::findSampleCodec(config.sampleType, codec);
		std::vector<float> in = decode(codec, supplied(rig.control(), 0));
		harness.requireEqual(in.size(), static_cast<size_t>(2 * 16), "the reopened stream supplied two periods");
		bool fromZero = true;
		for (size_t n = 0; n < in.size(); n++)
			fromZero = fromZero && std::fabs(in[n] - quantized(codec, FakeAsioDriver::generatorSample(config.seed, 0, n))) < 1e-6f;
		harness.expect(fromZero, "after the reopen the input starts again at generator sample 0");
	}

	TEST_CASE(AsioTests, testOutputReadyPath)
	{
		FakeAsioConfig config;
		StreamOptions options;
		options.processInput = false;
		asiotest::HostStub::Options hostOptions;
		hostOptions.callOutputReady = true;
		Rig rig(config, options, hostOptions);
		rig.processor->gain = 0.5f;
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(0, 2);
		w->init(nullptr);
		rig.host->createBuffers(w, 32);
		w->start();
		rig.control()->pump(3);
		w->stop();
		w->disposeBuffers();

		harness.expectEqual(rig.counters().outputReadyCalls, 3ul, "outputReady was forwarded to the target every period");
		harness.expectEqual(rig.processor->calls, 3u, "output was committed exactly once per period");
		SampleCodec codec;
		eapo::asio::findSampleCodec(config.sampleType, codec);
		std::vector<float> out = decode(codec, captured(rig.control(), 1));
		harness.requireEqual(out.size(), static_cast<size_t>(96), "three periods captured through outputReady");
		bool processed = true;
		for (size_t n = 0; n < out.size(); n++)
			processed = processed && std::fabs(out[n] - quantized(codec, rig.host->outputSample(1, n)) * 0.5f) < 1e-6f;
		harness.expect(processed, "output committed at outputReady carries the processed audio");
	}

	TEST_CASE(AsioTests, testUnopenedChannelsSeeSilence)
	{
		FakeAsioConfig config;
		config.outputChannels = 4;
		StreamOptions options;
		options.processInput = false;
		Rig rig(config, options, asiotest::HostStub::Options());
		rig.processor->gain = 1.0f;
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannelList({}, {0, 2});
		w->init(nullptr);
		rig.host->createBuffers(w, 16);
		harness.expectEqual(rig.processor->openedWith.channels[0], 4u, "the processor sees all four physical outputs");
		harness.expectEqual(rig.counters().createBuffersCalls, 1ul, "the target's buffers were created once");
		w->start();
		rig.control()->pump(2);
		w->stop();
		w->disposeBuffers();

		SampleCodec codec;
		eapo::asio::findSampleCodec(config.sampleType, codec);
		std::vector<float> ch1 = decode(codec, captured(rig.control(), 1));
		std::vector<float> ch2 = decode(codec, captured(rig.control(), 2));
		harness.requireEqual(ch1.size(), static_cast<size_t>(32), "the unopened channel exists on the target");
		bool silent = true;
		for (float sample : ch1)
			silent = silent && sample == 0.0f;
		harness.expect(silent, "a channel the host did not open plays silence");
		bool live = false;
		for (float sample : ch2)
			live = live || sample != 0.0f;
		harness.expect(live, "a channel the host opened plays its signal");
	}

	TEST_CASE(AsioTests, testResetAndRateChangePropagate)
	{
		FakeAsioConfig config;
		StreamOptions options;
		options.processInput = false;
		Rig rig(config, options, asiotest::HostStub::Options());
		rig.processor->gain = 0.5f;
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(0, 1);
		w->init(nullptr);
		rig.host->createBuffers(w, 16);
		w->start();

		rig.control()->raiseResetRequest();
		harness.expectEqual(rig.host->resetRequests(), 1ul, "the target's reset request reaches the host");
		harness.expectEqual(rig.counters().lastResetRequestAnswer, 1L, "the host's answer travels back to the target");

		rig.control()->pump(1);
		rig.control()->raiseSampleRateChange(96000.0);
		harness.expectEqual(rig.host->rateChanges(), 1ul, "sampleRateDidChange reaches the host");
		harness.expectNear(rig.host->lastRateChange(), 96000.0, 0.0, "with the new rate");
		harness.expectEqual(rig.host->resetRequests(), 2ul, "the wrapper asks the host to reset after a rate change");
		rig.control()->pump(2);
		harness.expectEqual(rig.processor->calls, 1u, "blocks after the rate change are not processed");
		w->stop();
		w->disposeBuffers();
		harness.expectEqual(rig.wrapper->stats().staleBlocks, 2u, "stale blocks were counted");

		SampleCodec codec;
		eapo::asio::findSampleCodec(config.sampleType, codec);
		std::vector<float> out = decode(codec, captured(rig.control(), 0));
		harness.requireEqual(out.size(), static_cast<size_t>(48), "three periods captured");
		bool stalePassed = true;
		for (size_t n = 16; n < 48; n++)
			stalePassed = stalePassed && std::fabs(out[n] - quantized(codec, rig.host->outputSample(0, n))) < 1e-6f;
		harness.expect(stalePassed, "stale blocks pass through untouched");

		harness.expectEqual(rig.host->createBuffers(w, 16), ASE_OK, "the reopen after the rate change succeeds");
		harness.expectNear(rig.processor->openedWith.sampleRate, 96000.0, 0.0, "the reopen carries the new rate");
		w->disposeBuffers();
	}

	TEST_CASE(AsioTests, testRejectedFormatsAndOpenFailures)
	{
		{
			FakeAsioConfig config;
			config.sampleType = ASIOSTDSDInt8LSB1;
			StreamOptions options;
			Rig rig(config, options, asiotest::HostStub::Options());
			AsioWrapper* w = rig.wrapper;
			rig.host->openChannels(1, 1);
			w->init(nullptr);
			harness.expectEqual(rig.host->createBuffers(w, 64), ASE_InvalidMode, "DSD is refused at createBuffers");
			harness.expectEqual(rig.counters().createBuffersCalls, 0ul, "the target never saw the DSD createBuffers");
			harness.expectEqual(rig.processor->opens, 0u, "the processor was never opened for DSD");
			char message[124] = {};
			w->getErrorMessage(message);
			harness.expect(std::strstr(message, "sample format") != nullptr, std::string("the error explains the format: ") + message);
			harness.expect(w->state() == AsioWrapper::State::Initialized, "the wrapper stays Initialized");
		}
		{
			FakeAsioConfig config;
			StreamOptions options;
			Rig rig(config, options, asiotest::HostStub::Options());
			rig.processor->openStatus = OpenReport::Status::Unavailable;
			AsioWrapper* w = rig.wrapper;
			rig.host->openChannels(1, 1);
			w->init(nullptr);
			harness.expectEqual(rig.host->createBuffers(w, 64), ASE_HWMalfunction, "an unavailable engine host fails createBuffers loudly");
			harness.expectEqual(rig.counters().createBuffersCalls, 1ul, "the target's buffers were created");
			harness.expectEqual(rig.counters().disposeBuffersCalls, 1ul, "and disposed again on the failure");
			harness.expectEqual(rig.processor->closes, 1u, "a failed processor open is closed immediately after callbacks drain");
			char message[124] = {};
			w->getErrorMessage(message);
			harness.expect(std::string(message) == "scripted failure", std::string("the processor's message is reported: ") + message);
			harness.expect(w->state() == AsioWrapper::State::Initialized, "the wrapper stays Initialized");
			harness.expect(rig.host->infos()[0].buffers[0] == nullptr, "the host's buffer pointers are cleared");
			rig.processor->openStatus = OpenReport::Status::Rejected;
			harness.expectEqual(rig.host->createBuffers(w, 64), ASE_InvalidMode, "a rejected format answers ASE_InvalidMode");
			harness.expectEqual(rig.processor->closes, 2u, "a rejected processor open is closed too");
			rig.processor->openStatus = OpenReport::Status::Ok;
			FILE* logFile = nullptr;
			harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "processor-open exception log opens");
			Logging::useStream(logFile, false, true, false);
			rig.processor->throwOnOpen = true;
			harness.expectEqual(rig.host->createBuffers(w, 64), ASE_HWMalfunction, "an exception from processor open fails createBuffers loudly");
			harness.expectEqual(rig.processor->closes, 3u, "a throwing processor open is closed after unwinding");
			harness.expectEqual(rig.counters().disposeBuffersCalls, 3ul, "each attempted processor open disposes the target buffers");
			rig.processor->throwOnOpen = false;
			rig.processor->throwUnknownOnOpen = true;
			harness.expectEqual(rig.host->createBuffers(w, 64), ASE_HWMalfunction, "an unknown processor-open exception fails createBuffers loudly");
			harness.expectEqual(rig.processor->closes, 4u, "an unknown throwing processor open is closed after unwinding");
			harness.expectEqual(rig.counters().disposeBuffersCalls, 4ul, "the unknown exception also disposes target buffers");
			std::fflush(logFile);
			std::rewind(logFile);
			std::wstring log;
			wchar_t text[1024] = {};
			while (std::fgetws(text, 1024, logFile) != nullptr)
				log.append(text);
			Logging::useStream(stdout, true, true, false);
			std::fclose(logFile);
			harness.expect(log.find(L"stream processor open threw a C++ exception: scripted open exception") != std::wstring::npos,
				"the processor-open C++ exception log names the site and what()");
			harness.expect(log.find(L"stream processor open threw an exception of unknown type") != std::wstring::npos,
				"the unknown processor-open exception log names the site");
		}
		{
			FakeAsioConfig config;
			config.failInit = 1;
			StreamOptions options;
			Rig rig(config, options, asiotest::HostStub::Options());
			AsioWrapper* w = rig.wrapper;
			harness.expectEqual(w->init(nullptr), ASIOFalse, "a target that fails init fails the wrapper's init");
			char message[124] = {};
			w->getErrorMessage(message);
			harness.expect(std::strstr(message, "FakeAsio") != nullptr, "the target's error message is passed on");
			harness.expect(w->state() == AsioWrapper::State::Loaded, "the wrapper stays Loaded");
		}
	}

	TEST_CASE(AsioTests, testLatenciesAddTheProcessorsShare)
	{
		FakeAsioConfig config;
		config.inputLatency = 10;
		config.outputLatency = 20;
		StreamOptions options;
		options.processInput = false;
		Rig rig(config, options, asiotest::HostStub::Options());
		rig.processor->extraLatency = 64;
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(1, 1);
		w->init(nullptr);
		long in = 0, out = 0;
		w->getLatencies(&in, &out);
		harness.expectEqual(in, 10L, "before buffers the input latency is the target's");
		harness.expectEqual(out, 20L, "before buffers the output latency is the target's");
		rig.host->createBuffers(w, 64);
		w->getLatencies(&in, &out);
		harness.expectEqual(in, 10L + 64L, "the disabled input direction adds nothing beyond the target's buffer");
		harness.expectEqual(out, 20L + 64L + 64L, "the processed output direction adds the processor's share");
		w->disposeBuffers();
	}

	TEST_CASE(AsioTests, testHostWithoutTimeInfo)
	{
		FakeAsioConfig config;
		StreamOptions options;
		asiotest::HostStub::Options hostOptions;
		hostOptions.supportsTimeInfo = false;
		Rig rig(config, options, hostOptions);
		AsioWrapper* w = rig.wrapper;
		rig.host->openChannels(1, 1);
		w->init(nullptr);
		rig.host->createBuffers(w, 32);
		harness.expectEqual(rig.counters().hostSupportsTimeInfo, 0L, "the target is told the host has no time info");
		w->start();
		rig.control()->pump(2);
		w->stop();
		w->disposeBuffers();
		harness.expectEqual(rig.host->switches(), 2ul, "switches reached the host");
		harness.expectEqual(rig.host->timeInfoSwitches(), 0ul, "through the plain callback");
		harness.expectEqual(rig.counters().timeInfoSwitches, 0ul, "and the target used the plain callback too");
	}

	// ---- registry record ----

	TEST_CASE(AsioTests, testSyncDeadline)
	{
		StreamFormat format;
		format.sampleRate = 48000.0;
		format.frames = 480;   // a 10 ms period
		StreamOptions options;
		harness.expectEqual(eapo::asio::syncDeadlineUs(format, options), 2500u, "the default deadline is a quarter of the period");
		options.deadlinePercent = 50;
		harness.expectEqual(eapo::asio::syncDeadlineUs(format, options), 5000u, "50 percent is half the period");
		options.deadlinePercent = 75;
		harness.expectEqual(eapo::asio::syncDeadlineUs(format, options), 7500u, "75 percent is three quarters");
		options.deadlineUs = 333;
		harness.expectEqual(eapo::asio::syncDeadlineUs(format, options), 333u, "an explicit budget wins over the share");
		options.deadlineUs = 0;
		format.sampleRate = 0.0;
		harness.expectEqual(eapo::asio::syncDeadlineUs(format, options), 0u, "no rate: no deadline");
	}

	TEST_CASE(AsioTests, testWrapperRecordRoundTrip)
	{
		test::FakeRegistry registry;
		eapo::asio::WrapperRecord record;
		record.wrapperClsid = L"{11111111-2222-3333-4444-555555555555}";
		record.targetClsid = L"{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}";
		record.targetName = L"Some USB ASIO";
		record.options.processInput = false;
		record.options.float64Samples = true;
		record.options.mode = Mode::Sync;
		record.options.deadlineUs = 333;
		record.options.readyTimeoutMs = 4444;
		record.options.lingerMs = 5555;
		record.options.deadlinePercent = 50;
		record.autoStart = true;
		record.register32 = true;

		eapo::asio::WrapperRecord missing;
		harness.expectFalse(eapo::asio::WrapperRecords::read(registry, record.wrapperClsid, missing), "no record before write");

		eapo::asio::WrapperRecords::write(registry, record);
		const std::wstring key = eapo::asio::WrapperRecords::recordKey(record.wrapperClsid);
		harness.expect(key == L"HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO\\ASIO\\{11111111-2222-3333-4444-555555555555}", "the record key spelling");
		harness.expect(registry.readValue(key, L"TargetClsid") == record.targetClsid, "TargetClsid is written");
		harness.expectEqual(registry.readDWORDValue(key, L"ProcessInput"), 0ul, "ProcessInput is written as a DWORD");
		harness.expectEqual(registry.readDWORDValue(key, L"Float64Samples"), 1ul, "Float64Samples is written as a DWORD");
		harness.expectEqual(registry.readDWORDValue(key, L"Mode"), 0ul, "Sync is Mode 0 (Pipelined, the default, is 1)");

		eapo::asio::WrapperRecord back;
		harness.require(eapo::asio::WrapperRecords::read(registry, record.wrapperClsid, back), "the record reads back");
		harness.expect(back.targetClsid == record.targetClsid, "TargetClsid round-trips");
		harness.expect(back.targetName == record.targetName, "TargetName round-trips");
		harness.expectFalse(back.options.processInput, "ProcessInput round-trips");
		harness.expectTrue(back.options.processOutput, "ProcessOutput round-trips");
		harness.expectTrue(back.options.float64Samples, "Float64Samples round-trips");
		harness.expect(back.options.mode == Mode::Sync, "Mode round-trips");
		harness.expectEqual(back.options.deadlineUs, 333u, "DeadlineUs round-trips");
		harness.expectEqual(back.options.readyTimeoutMs, 4444u, "ReadyTimeoutMs round-trips");
		harness.expectEqual(back.options.lingerMs, 5555u, "LingerMs round-trips");
		harness.expectEqual(back.options.deadlinePercent, 50u, "DeadlinePercent round-trips");
		harness.expectTrue(back.autoStart, "AutoStart round-trips");
		harness.expectTrue(back.register32, "Register32 round-trips");

		// A record written by an older build without the optional values still reads.
		const std::wstring sparseKey = eapo::asio::WrapperRecords::recordKey(L"{99999999-2222-3333-4444-555555555555}");
		registry.seedKey(sparseKey);
		registry.seedString(sparseKey, L"TargetClsid", L"{F}");
		eapo::asio::WrapperRecord sparse;
		harness.require(eapo::asio::WrapperRecords::read(registry, L"{99999999-2222-3333-4444-555555555555}", sparse), "a sparse record reads");
		harness.expectTrue(sparse.options.processInput, "missing values take the defaults");
		harness.expect(sparse.options.mode == Mode::Pipelined, "a record without Mode runs pipelined, the default");
		harness.expectEqual(sparse.options.readyTimeoutMs, StreamOptions().readyTimeoutMs, "including the ready timeout");
		harness.expectEqual(sparse.options.deadlinePercent, 0u, "DeadlinePercent 0 = the quarter default");
		harness.expectFalse(sparse.autoStart, "a record without AutoStart does not start the host at boot");
		harness.expectFalse(sparse.register32, "a record without Register32 serves 64-bit hosts only");
		harness.expectFalse(sparse.options.float64Samples, "a record without Float64Samples uses the target type");

		eapo::asio::EntryOptions entryOptions = eapo::asio::WrapperRecords::entryOptions(back);
		harness.expectTrue(entryOptions.float64Samples, "entryOptions carries Float64Samples from the record");
		entryOptions.float64Samples = false;
		eapo::asio::WrapperRecords::setEntryOptions(back, entryOptions);
		harness.expectFalse(back.options.float64Samples, "setEntryOptions carries Float64Samples to the record");

		eapo::asio::WrapperRecords::remove(registry, record.wrapperClsid);
		harness.expectFalse(registry.keyExists(key), "remove deletes the record");
		eapo::asio::WrapperRecords::remove(registry, record.wrapperClsid);
		harness.expect(true, "removing a missing record is not an error");
	}

	// ---- loopback latency matching ----

	TEST_CASE(AsioTests, testLatencyMatchingAndSummary)
	{
		const std::vector<int64_t> emitted = {0, 5000000, 10000000};
		asiotest::LatencyMatch match = asiotest::matchClicks(emitted, {1200, 5001200, 10001200}, 5000000);
		harness.expectEqual(match.matched, static_cast<size_t>(3), "constant latency matches every click");
		harness.expectEqual(match.missing, static_cast<size_t>(0), "constant latency has no missing click");
		harness.expectEqual(match.latency100ns[1], static_cast<int64_t>(1200), "constant latency is retained per click");
		asiotest::LatencySummary summary = asiotest::summarize(emitted, match, 500.0);
		harness.expectNear(summary.meanUs, 120.0, 1e-9, "constant latency mean");
		harness.expectNear(summary.stdevUs, 0.0, 1e-9, "constant latency deviation");

		match = asiotest::matchClicks(emitted, {1200, 10001200}, 5000000);
		harness.expectEqual(match.matched, static_cast<size_t>(2), "onsets on either side of a gap still match");
		harness.expectEqual(match.missing, static_cast<size_t>(1), "one absent onset is missing");
		harness.expectEqual(match.latency100ns[1], static_cast<int64_t>(INT64_MIN), "the missing click carries the sentinel");

		match = asiotest::matchClicks({0}, {1000, 2000}, 5000000);
		harness.expectEqual(match.matched, static_cast<size_t>(1), "the first onset is consumed");
		harness.expectEqual(match.extra, static_cast<size_t>(1), "an unused onset is extra");

		const std::vector<int64_t> stepEmitted = {0, 5000000, 10000000, 15000000};
		match = asiotest::matchClicks(stepEmitted, {1000, 5001000, 10011000, 15011000}, 5000000);
		summary = asiotest::summarize(stepEmitted, match, 500.0);
		harness.expectEqual(summary.steps, static_cast<size_t>(1), "one middle latency jump is one step");
		harness.expectNear(summary.largestStepUs, 1000.0, 1e-9, "the largest step keeps its positive sign");

		constexpr int64_t halfMinute = 30LL * 10000000LL;
		const std::vector<int64_t> driftEmitted = {0, halfMinute, halfMinute * 2, halfMinute * 3};
		std::vector<int64_t> driftOnsets;
		for (size_t i = 0; i < driftEmitted.size(); i++)
			driftOnsets.push_back(driftEmitted[i] + 1000 + static_cast<int64_t>(i) * 600);
		match = asiotest::matchClicks(driftEmitted, driftOnsets, halfMinute);
		summary = asiotest::summarize(driftEmitted, match, 500.0);
		harness.expectNear(summary.slopeUsPerMinute, 120.0, 1e-9, "linear drift reports microseconds per minute");

		match = asiotest::matchClicks({}, {}, 5000000);
		summary = asiotest::summarize({}, match, 500.0);
		harness.expectEqual(summary.count, static_cast<size_t>(0), "empty latency input has an empty summary");
		harness.expectNear(summary.meanUs, 0.0, 0.0, "empty latency input keeps zero statistics");
	}

	// ---- in-process engine adapter ----

	TEST_CASE(AsioTests, testInProcProcessorRunsTheEngine)
	{
		test::TestDirectory directory(L"AsioTests");
		const std::wstring configPath = directory.trackFile(L"config.txt");
		{
			std::ofstream file(configPath);
			file << "Channel: R\nPreamp: -6.0206 dB\n";
		}

		FakeAsioConfig config;
		config.sampleType = ASIOSTFloat32LSB;
		StreamOptions options;
		options.configPath = configPath;
		options.processInput = false;
		FakeAsioDriver* fake = new FakeAsioDriver();
		fake->configure(&config);
		AsioWrapper* w = new AsioWrapper(fake, testWrapperClsid, testTargetClsid, options, std::make_unique<eapo::asio::InProcProcessor>());
		fake->Release();
		asiotest::HostStub::Options hostOptions;
		hostOptions.sampleType = ASIOSTFloat32LSB;
		{
			asiotest::HostStub host(hostOptions);
			host.openChannels(0, 2);
			harness.require(w->init(nullptr) == ASIOTrue, "init");
			harness.requireEqual(host.createBuffers(w, 64), ASE_OK, "createBuffers with the in-process engine");
			harness.requireEqual(w->start(), ASE_OK, "start");
			fake->pump(3);
			w->stop();
			w->disposeBuffers();

			SampleCodec codec;
			eapo::asio::findSampleCodec(ASIOSTFloat32LSB, codec);
			std::vector<float> left = decode(codec, captured(fake, 0));
			std::vector<float> right = decode(codec, captured(fake, 1));
			harness.requireEqual(left.size(), static_cast<size_t>(192), "three periods on L");
			bool leftUntouched = true, rightHalved = true, firstBlockHalved = true;
			for (size_t n = 0; n < 192; n++)
			{
				leftUntouched = leftUntouched && std::fabs(left[n] - host.outputSample(0, n)) < 1e-6f;
				const bool halved = std::fabs(right[n] - host.outputSample(1, n) * 0.5f) < 2e-4f;
				rightHalved = rightHalved && halved;
				if (n < 64)
					firstBlockHalved = firstBlockHalved && halved;
			}
			harness.expect(leftUntouched, "Channel: R leaves L alone (channel names come from the channel count)");
			harness.expect(rightHalved, "the -6 dB preamp halves R");
			harness.expect(firstBlockHalved, "the first block is already filtered: the config loaded inside createBuffers");
		}
		w->Release();
		directory.removeAll();
	}

	TEST_CASE(AsioTests, testAllInstancesReleased)
	{
		harness.expectEqual(AsioWrapper::instanceCount(), 0L, "every wrapper was released");
		harness.expectEqual(FakeAsioDriver::instanceCount(), 0L, "every fake driver was released");
	}
}

int main()
{
	Logging::set(stderr, false, false, false);
	try
	{
		return test::runAll("AsioTests", {
			"StreamRingTests",
			"DaemonTests",
			"DeviceRecordTests",
			"ReceiverKeepaliveTests",
			"PrecisionTests",
			"AsioTests",
		});
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "AsioTests: unhandled exception: %s\n", error.what());
	}
	catch (...)
	{
		std::fprintf(stderr, "AsioTests: unhandled non-standard exception\n");
	}
	return 1;
}
