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

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "asio/AsioWrapper.h"
#include "asio/CallbackTrampolines.h"
#include "asio/InProcProcessor.h"
#include "asio/SampleCodec.h"
#include "asio/StreamProcessor.h"
#include "asio/WasapiExclusiveTarget.h"
#include "asio/WrapperRecord.h"
#include "services/logging/Logging.h"
#include "Tests/AlignedMemoryGate.h"
#include "Tests/AsioSupport/HostStub.h"
#include "Tests/EngineOrchestrationTests/FakeRegistry.h"
#include "Tests/FakeAsioDriver/FakeAsio.h"
#include "Tests/TestDirectory.h"
#include "Tests/TestHarness.h"

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
	test::Harness harness("AsioTests");

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
		unsigned opens = 0, closes = 0, calls = 0;
		StreamStats closedWith;
		StreamFormat openedWith;

		OpenReport open(const StreamFormat& format, const StreamOptions&) override
		{
			opens++;
			openedWith = format;
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
				storage_[slot].assign(static_cast<size_t>(channels) * format.frames, 0.0f);
				planes_[slot].resize(channels);
				for (unsigned c = 0; c < channels; c++)
					planes_[slot][c] = storage_[slot].data() + static_cast<size_t>(c) * format.frames;
				report.planes[slot] = channels > 0 ? planes_[slot].data() : nullptr;
			}
			report.extraLatencyFrames = extraLatency;
			return report;
		}

		Outcome process(Direction direction) noexcept override
		{
			const unsigned index = calls++;
			if (index < script.size() && script[index] != Outcome::Processed)
				return script[index];
			const unsigned slot = static_cast<unsigned>(direction);
			for (float& sample : storage_[slot])
				sample *= gain;
			return Outcome::Processed;
		}

		void close(const StreamStats& stats) noexcept override
		{
			closes++;
			closedWith = stats;
		}

	private:
		std::vector<float> storage_[eapo::asio::directionCount];
		std::vector<float*> planes_[eapo::asio::directionCount];
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
			wrapper = new AsioWrapper(fake, testWrapperClsid, testTargetClsid, std::move(options), std::unique_ptr<IStreamProcessor>(processor));
			fake->Release();       // the wrapper holds the reference now
			hostOptions.sampleType = config.sampleType;
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

	std::vector<float> decode(const SampleCodec& codec, const std::vector<unsigned char>& bytes)
	{
		std::vector<float> samples(bytes.size() / codec.bytesPerSample);
		codec.toFloat(bytes.data(), samples.data(), static_cast<unsigned>(samples.size()));
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

	// Applies the codec round trip the host's float takes before the wrapper
	// sees it, so expectations quantize the same way the buffers do.
	float quantized(const SampleCodec& codec, float value)
	{
		unsigned char bytes[8] = {};
		float back = 0.0f;
		codec.fromFloat(&value, bytes, 1);
		codec.toFloat(bytes, &back, 1);
		return back;
	}

	// ---- codecs ----

	void testCodecRoundTrips()
	{
		const long types[] = {
			ASIOSTInt16LSB, ASIOSTInt16MSB, ASIOSTInt24LSB, ASIOSTInt24MSB, ASIOSTInt32LSB, ASIOSTInt32MSB,
			ASIOSTFloat32LSB, ASIOSTFloat32MSB, ASIOSTFloat64LSB, ASIOSTFloat64MSB,
			ASIOSTInt32LSB16, ASIOSTInt32LSB18, ASIOSTInt32LSB20, ASIOSTInt32LSB24,
			ASIOSTInt32MSB16, ASIOSTInt32MSB18, ASIOSTInt32MSB20, ASIOSTInt32MSB24
		};
		const float probe[] = {0.0f, 0.5f, -0.5f, 0.999f, -1.0f, 0.123456f, -0.000123f, 1.5f, -2.0f};
		for (long type : types)
		{
			SampleCodec codec;
			harness.require(eapo::asio::findSampleCodec(type, codec), "codec exists for type " + std::to_string(type));
			harness.expectEqual(codec.type, type, "codec reports its own type");
			std::vector<unsigned char> bytes(codec.bytesPerSample * 9);
			float back[9] = {};
			codec.fromFloat(probe, bytes.data(), 9);
			codec.toFloat(bytes.data(), back, 9);
			const double tolerance = type == ASIOSTInt16LSB || type == ASIOSTInt16MSB
				|| type == ASIOSTInt32LSB16 || type == ASIOSTInt32MSB16 ? 1.0 / 32768.0 : 1.0 / 131072.0;
			const bool floating = type == ASIOSTFloat32LSB || type == ASIOSTFloat32MSB
				|| type == ASIOSTFloat64LSB || type == ASIOSTFloat64MSB;
			for (int i = 0; i < 9; i++)
			{
				float expected = probe[i];
				// Integer types clamp at full scale; float types carry
				// over-range values through untouched.
				if (!floating && expected > 1.0f)
					expected = 1.0f;
				if (!floating && expected < -1.0f)
					expected = -1.0f;
				harness.expectNear(back[i], expected, tolerance,
					"type " + std::to_string(type) + " round-trips sample " + std::to_string(i));
			}
		}

		SampleCodec dsd;
		harness.expectFalse(eapo::asio::findSampleCodec(ASIOSTDSDInt8LSB1, dsd), "DSD has no codec");
		harness.expectFalse(eapo::asio::findSampleCodec(12345, dsd), "an unknown type has no codec");
	}

	// The WASAPI target's pure parts: which containers are tried in which
	// order, what buffer sizes it offers, and the byte shuffles between the
	// ASIO planes and the device's interleaved block.
	void testWasapiTargetPolicy()
	{
		namespace wasapi = eapo::asio::wasapi;

		// The endpoint's own format leads; the fixed list follows without repeats.
		WAVEFORMATEXTENSIBLE sixteen = wasapi::makeFormat(wasapi::Container{ASIOSTInt16LSB, 16, 16, false}, 2, 48000, 3);
		std::vector<wasapi::Container> order = wasapi::containerCandidates(&sixteen.Format);
		harness.require(order.size() == 5, "five containers are tried for a 16-bit device format");
		harness.expectEqual(order[0].asioType, static_cast<long>(ASIOSTInt16LSB), "the device's own container comes first");
		harness.expectEqual(order[1].asioType, static_cast<long>(ASIOSTFloat32LSB), "float follows");
		harness.expectEqual(order[2].asioType, static_cast<long>(ASIOSTInt32LSB), "then 32 bit");
		harness.expectEqual(order[3].asioType, static_cast<long>(ASIOSTInt32LSB24), "then 24 in 32");
		harness.expectEqual(order[4].asioType, static_cast<long>(ASIOSTInt24LSB), "then packed 24; 16 bit is not repeated");

		WAVEFORMATEXTENSIBLE twentyFour = wasapi::makeFormat(wasapi::Container{ASIOSTInt32LSB24, 32, 24, false}, 2, 96000, 3);
		order = wasapi::containerCandidates(&twentyFour.Format);
		harness.require(order.size() == 5, "five containers for a 24-in-32 device format");
		harness.expectEqual(order[0].asioType, static_cast<long>(ASIOSTInt32LSB24), "24 valid bits in 32 is recognised from the extensible header");
		harness.expectEqual(order[4].asioType, static_cast<long>(ASIOSTInt16LSB), "16 bit closes the list");

		order = wasapi::containerCandidates(nullptr);
		harness.expectEqual(order.size(), static_cast<size_t>(5), "no device format: the fixed list alone");
		harness.expectEqual(order[0].asioType, static_cast<long>(ASIOSTFloat32LSB), "which starts with float");

		WAVEFORMATEX plain = {};
		plain.wFormatTag = WAVE_FORMAT_PCM;
		plain.nChannels = 2;
		plain.nSamplesPerSec = 44100;
		plain.wBitsPerSample = 24;
		order = wasapi::containerCandidates(&plain);
		harness.expectEqual(order[0].asioType, static_cast<long>(ASIOSTInt24LSB), "a plain 24-bit PCM header is packed 24");

		// The extensible format a container becomes.
		harness.expectEqual(static_cast<int>(twentyFour.Format.nBlockAlign), 8, "24 in 32 on two channels is 8 bytes a frame");
		harness.expectEqual(static_cast<int>(twentyFour.Samples.wValidBitsPerSample), 24, "valid bits carried into the header");
		harness.expectTrue(twentyFour.SubFormat == KSDATAFORMAT_SUBTYPE_PCM, "integer containers are PCM");
		WAVEFORMATEXTENSIBLE floating = wasapi::makeFormat(wasapi::Container{ASIOSTFloat32LSB, 32, 32, true}, 8, 48000, 0x63f);
		harness.expectTrue(floating.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, "float is IEEE float");
		harness.expectEqual(static_cast<unsigned long>(floating.dwChannelMask), 0x63ful, "the channel mask is passed through");
		harness.expectEqual(floating.Format.nAvgBytesPerSec, 48000u * 32u, "bytes per second follow the layout");

		// Buffer sizes: powers of two from the smallest at or above the device minimum.
		wasapi::BufferPolicy policy = wasapi::bufferPolicy(144);       // 3 ms at 48 kHz
		harness.expectEqual(policy.minSize, 256L, "3 ms at 48 kHz offers 256 frames as the smallest");
		harness.expectEqual(policy.preferredSize, 256L, "and prefers it");
		harness.expectEqual(policy.maxSize, 2048L, "up to 2048");
		harness.expectEqual(policy.granularity, -1L, "in powers of two");
		policy = wasapi::bufferPolicy(96);
		harness.expectEqual(policy.minSize, 128L, "2 ms at 48 kHz offers 128");
		policy = wasapi::bufferPolicy(0);
		harness.expectEqual(policy.minSize, 32L, "never below 32");
		policy = wasapi::bufferPolicy(4000);
		harness.expectEqual(policy.minSize, 4096L, "a huge minimum is honoured");
		harness.expectEqual(policy.maxSize, 4096L, "and the maximum follows it up");
		const unsigned minPeriod48 = wasapi::framesFromHns(30000, 48000);
		harness.expectEqual(minPeriod48, 144u, "a 3 ms minimum is 144 frames at 48 kHz");
		harness.expectEqual(wasapi::bufferPolicy(minPeriod48).minSize, 256L, "the 48 kHz minimum offers 256 frames");
		const unsigned minPeriod96 = wasapi::framesFromHns(30000, 96000);
		harness.expectEqual(minPeriod96, 288u, "the same 3 ms minimum is 288 frames at 96 kHz");
		harness.expectEqual(wasapi::bufferPolicy(minPeriod96).minSize, 512L, "the 96 kHz minimum offers 512 frames");

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
			for (unsigned c = 0; c < channels; c++)
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

	void testCodecBytePatterns()
	{
		SampleCodec codec;
		eapo::asio::findSampleCodec(ASIOSTInt16LSB, codec);
		const unsigned char maxLsb[2] = {0xff, 0x7f};
		float value = 0.0f;
		codec.toFloat(maxLsb, &value, 1);
		harness.expectNear(value, 32767.0 / 32768.0, 1e-7, "Int16LSB 0x7fff is just under full scale");

		eapo::asio::findSampleCodec(ASIOSTInt16MSB, codec);
		const unsigned char maxMsb[2] = {0x80, 0x00};
		codec.toFloat(maxMsb, &value, 1);
		harness.expectNear(value, -1.0, 1e-7, "Int16MSB 0x8000 is negative full scale");

		eapo::asio::findSampleCodec(ASIOSTInt24LSB, codec);
		const unsigned char minus24[3] = {0x00, 0x00, 0x80};
		codec.toFloat(minus24, &value, 1);
		harness.expectNear(value, -1.0, 1e-7, "Int24LSB sign-extends from bit 23");

		eapo::asio::findSampleCodec(ASIOSTInt32LSB24, codec);
		const unsigned char low24[4] = {0xff, 0xff, 0x7f, 0x00};
		codec.toFloat(low24, &value, 1);
		harness.expectNear(value, 8388607.0 / 8388608.0, 1e-7, "Int32LSB24 keeps its 24 valid bits right-aligned");
		const unsigned char neg24[4] = {0x00, 0x00, 0x80, 0x00};
		codec.toFloat(neg24, &value, 1);
		harness.expectNear(value, -1.0, 1e-7, "Int32LSB24 sign-extends from bit 23");
		unsigned char written[4] = {};
		const float minusOne = -1.0f;
		codec.fromFloat(&minusOne, written, 1);
		harness.expectEqual(static_cast<int>(written[3]), 0, "Int32LSB24 writes nothing above its 24 bits");

		eapo::asio::findSampleCodec(ASIOSTFloat32MSB, codec);
		const float one = 1.0f;
		codec.fromFloat(&one, written, 1);
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

	void testTrampolines()
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

	// ---- wrapper: state machine ----

	void testStateMachineOrdering()
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

	// ---- wrapper: data flow ----

	void testOutputAndInputAreProcessed()
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
		harness.expectEqual(rig.processor->closedWith.blocks[0], 4ull, "output blocks were counted");
		harness.expectEqual(rig.processor->closedWith.blocks[1], 4ull, "input blocks were counted");
		harness.expectEqual(rig.processor->closedWith.late[0] + rig.processor->closedWith.late[1], 0ull, "nothing was late");
		harness.expectEqual(rig.counters().timeInfoSwitches, 4ul, "the target used the time-info switch");
		harness.expectEqual(rig.host->timeInfoSwitches(), 4ul, "the host received the time-info switch");
	}

	void testFirstBlockIsAlreadyProcessed()
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

	void testLateAndOffPassThrough()
	{
		FakeAsioConfig config;
		StreamOptions options;
		options.processInput = false;
		Rig rig(config, options, asiotest::HostStub::Options());
		rig.processor->gain = 0.5f;
		rig.processor->script = {Outcome::Processed, Outcome::Late, Outcome::Processed};
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
		harness.expectEqual(rig.processor->closedWith.late[0], 1ull, "the late block was counted");
		harness.expectEqual(rig.processor->closedWith.blocks[1], 0ull, "the disabled input direction was never processed");

		std::vector<float> in = decode(codec, rig.host->inputRecord(0));
		bool inputRaw = true;
		for (size_t n = 0; n < in.size(); n++)
			inputRaw = inputRaw && std::fabs(in[n] - quantized(codec, FakeAsioDriver::generatorSample(config.seed, 0, n))) < 1e-6f;
		harness.expect(inputRaw, "a disabled input direction is copied byte for byte");
	}

	void testGoneIsStickyUntilReopen()
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
		harness.expectEqual(rig.processor->closedWith.gone[0], 1ull, "the Gone block was counted");
		rig.processor->script.clear();
		harness.expectEqual(rig.host->createBuffers(w, 16), ASE_OK, "reopen after Gone");
		harness.expectEqual(w->start(), ASE_OK, "start after the reopen works again");
		w->stop();
		w->disposeBuffers();
	}

	void testOutputReadyPath()
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

	void testUnopenedChannelsSeeSilence()
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

	void testResetAndRateChangePropagate()
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
		harness.expectEqual(rig.processor->closedWith.staleBlocks, 2u, "stale blocks were counted");

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

	void testRejectedFormatsAndOpenFailures()
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
			char message[124] = {};
			w->getErrorMessage(message);
			harness.expect(std::string(message) == "scripted failure", std::string("the processor's message is reported: ") + message);
			harness.expect(w->state() == AsioWrapper::State::Initialized, "the wrapper stays Initialized");
			harness.expect(rig.host->infos()[0].buffers[0] == nullptr, "the host's buffer pointers are cleared");
			rig.processor->openStatus = OpenReport::Status::Rejected;
			harness.expectEqual(rig.host->createBuffers(w, 64), ASE_InvalidMode, "a rejected format answers ASE_InvalidMode");
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

	void testLatenciesAddTheProcessorsShare()
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

	void testHostWithoutTimeInfo()
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

	void testSyncDeadline()
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

	void testWrapperRecordRoundTrip()
	{
		test::FakeRegistry registry;
		eapo::asio::WrapperRecord record;
		record.wrapperClsid = L"{11111111-2222-3333-4444-555555555555}";
		record.targetClsid = L"{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}";
		record.targetName = L"Some USB ASIO";
		record.options.processInput = false;
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
		harness.expectEqual(registry.readDWORDValue(key, L"Mode"), 0ul, "Sync is Mode 0 (Pipelined, the default, is 1)");

		eapo::asio::WrapperRecord back;
		harness.require(eapo::asio::WrapperRecords::read(registry, record.wrapperClsid, back), "the record reads back");
		harness.expect(back.targetClsid == record.targetClsid, "TargetClsid round-trips");
		harness.expect(back.targetName == record.targetName, "TargetName round-trips");
		harness.expectFalse(back.options.processInput, "ProcessInput round-trips");
		harness.expectTrue(back.options.processOutput, "ProcessOutput round-trips");
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

		eapo::asio::WrapperRecords::remove(registry, record.wrapperClsid);
		harness.expectFalse(registry.keyExists(key), "remove deletes the record");
		eapo::asio::WrapperRecords::remove(registry, record.wrapperClsid);
		harness.expect(true, "removing a missing record is not an error");
	}

	// ---- in-process engine adapter ----

	void testInProcProcessorRunsTheEngine()
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

	int runAsioTests()
	{
		testCodecRoundTrips();
		testWasapiTargetPolicy();
		testCodecBytePatterns();
		testTrampolines();
		testStateMachineOrdering();
		testOutputAndInputAreProcessed();
		testFirstBlockIsAlreadyProcessed();
		testLateAndOffPassThrough();
		testGoneIsStickyUntilReopen();
		testOutputReadyPath();
		testUnopenedChannelsSeeSilence();
		testResetAndRateChangePropagate();
		testRejectedFormatsAndOpenFailures();
		testLatenciesAddTheProcessorsShare();
		testHostWithoutTimeInfo();
		testSyncDeadline();
		testWrapperRecordRoundTrip();
		testInProcProcessorRunsTheEngine();
		harness.expectEqual(AsioWrapper::instanceCount(), 0L, "every wrapper was released");
		harness.expectEqual(FakeAsioDriver::instanceCount(), 0L, "every fake driver was released");
		harness.report();
		return test::reportAlignedMemoryBalance("AsioTests");
	}
}

int runStreamRingTests();
int runDaemonTests();
int runDeviceRecordTests();

int main()
{
	Logging::set(stderr, false, false, false);
	try
	{
		if (runStreamRingTests() != 0)
			return 1;
		if (runDaemonTests() != 0)
			return 1;
		if (runDeviceRecordTests() != 0)
			return 1;
		return runAsioTests();
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
