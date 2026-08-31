/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The daemon adapter over the thread host: the same DaemonProcessor,
	StreamRing and EngineHostCore the shipped wrapper and host run, with the
	host on a thread in this process. Pins that the adapter hashes exactly
	like the in-process one (transparency), the pipelined shape (one silent
	block, then everything one period late, latency reported), a host that
	dies mid-stream (Gone, sticky, and a clean reopen), and the loud failure
	when the real host executable is missing.
*/

#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "asio/AsioWrapper.h"
#include "asio/DaemonProcessor.h"
#include "asio/InProcProcessor.h"
#include "asio/SampleCodec.h"
#include "asio/ThreadHostLink.h"
#include "asio/Win32HostLink.h"
#include "Tests/AsioSupport/HostStub.h"
#include "Tests/FakeAsioDriver/FakeAsio.h"
#include "Tests/TestDirectory.h"
#include "Tests/TestHarness.h"

using eapo::asio::AsioWrapper;
using eapo::asio::DaemonProcessor;
using eapo::asio::IStreamProcessor;
using eapo::asio::Mode;
using eapo::asio::SampleCodec;
using eapo::asio::StreamOptions;
using eapo::asio::ThreadHostLink;

namespace
{
	test::Harness harness("DaemonTests");

	constexpr GUID testWrapperClsid = {0xc0ffee01, 0x1111, 0x4222, {0x83, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66}};
	const wchar_t* const testTargetClsid = L"{B7E3A9F4-52C1-4D0B-8A6E-1F9C3D5E7B21}";

	struct Capture
	{
		std::vector<std::vector<unsigned char>> outputs;
		std::vector<std::vector<unsigned char>> inputs;
		long inputLatency = 0;
		long outputLatency = 0;
		eapo::asio::StreamStats stats;
		bool started = false;
	};

	// Runs one stream over `processor` and returns what reached the fake's
	// outputs and the host's inputs.
	Capture runStream(std::unique_ptr<IStreamProcessor> processor, const StreamOptions& options, long frames, long periods,
		long inputs, long outputs, AsioWrapper** keep = nullptr, FakeAsioDriver** keepFake = nullptr)
	{
		Capture capture;
		FakeAsioConfig config;
		config.sampleType = ASIOSTInt32LSB;
		config.inputChannels = inputs;
		config.outputChannels = outputs;
		FakeAsioDriver* fake = new FakeAsioDriver();
		fake->configure(&config);
		AsioWrapper* wrapper = new AsioWrapper(fake, testWrapperClsid, testTargetClsid, options, std::move(processor));
		fake->Release();
		asiotest::HostStub::Options hostOptions;
		hostOptions.sampleType = ASIOSTInt32LSB;
		{
			asiotest::HostStub host(hostOptions);
			host.openChannels(inputs, outputs);
			wrapper->init(nullptr);
			if (host.createBuffers(wrapper, frames) == ASE_OK)
			{
				wrapper->getLatencies(&capture.inputLatency, &capture.outputLatency);
				if (wrapper->start() == ASE_OK)
				{
					capture.started = true;
					fake->pump(periods);
					wrapper->stop();
				}
				capture.stats = wrapper->stats();
				wrapper->disposeBuffers();
			}
			for (long c = 0; c < outputs; c++)
			{
				const unsigned char* data = nullptr;
				unsigned long bytes = 0;
				fake->capturedOutput(c, &data, &bytes);
				capture.outputs.emplace_back(data, data + bytes);
			}
			for (long c = 0; c < inputs; c++)
				capture.inputs.push_back(host.inputRecord(static_cast<size_t>(c)));
		}
		if (keep != nullptr)
		{
			*keep = wrapper;
			*keepFake = fake;
		}
		else
		{
			wrapper->Release();
		}
		return capture;
	}

	std::wstring writeConfig(test::TestDirectory& directory, const char* text)
	{
		const std::wstring path = directory.trackFile(L"config.txt");
		std::ofstream file(path);
		file << text;
		return path;
	}

	void testDaemonMatchesInProc()
	{
		test::TestDirectory directory(L"DaemonTests");
		const std::wstring configPath = writeConfig(directory,
			"Preamp: -6.0206 dB\nFilter: ON PK Fc 1000 Hz Gain -3 dB Q 1.0\nChannel: R\nDelay: 3 samples\n");
		StreamOptions options;
		options.configPath = configPath;
		options.readyTimeoutMs = 20000;
		options.mode = Mode::Sync;
		// This test compares bytes, not timing: the automatic deadline (a
		// quarter period) is what the probe gate measures, and a loaded box
		// misses it through the plain thread host.
		options.deadlineUs = 20000000;

		Capture inproc = runStream(std::make_unique<eapo::asio::InProcProcessor>(), options, 64, 40, 2, 2);
		Capture daemon = runStream(std::make_unique<DaemonProcessor>(std::make_unique<ThreadHostLink>()), options, 64, 40, 2, 2);
		harness.require(inproc.started && daemon.started, "both streams started");
		harness.expect(daemon.outputs == inproc.outputs, "the daemon adapter's output is byte-identical to the in-process adapter's");
		harness.expect(daemon.inputs == inproc.inputs, "and so is the captured input");
		harness.expectEqual(daemon.outputLatency, inproc.outputLatency, "sync mode adds no latency");
		harness.expectEqual(daemon.stats.late[0] + daemon.stats.late[1], 0ull, "no block was late at a 20 s deadline");
		harness.expectEqual(daemon.stats.blocks[0], 40ull, "every output block went through the ring");
		directory.removeAll();
	}

	void testPipelinedShape()
	{
		test::TestDirectory directory(L"DaemonTests");
		const std::wstring configPath = writeConfig(directory, "Preamp: -6.0206 dB\n");
		StreamOptions options;
		options.configPath = configPath;
		options.processInput = false;
		options.mode = Mode::Sync;
		options.deadlineUs = 20000000;   // shape, not timing (see above)

		constexpr long frames = 256;
		Capture sync = runStream(std::make_unique<DaemonProcessor>(std::make_unique<ThreadHostLink>()), options, frames, 10, 0, 1);
		options.mode = Mode::Pipelined;
		Capture pipelined = runStream(std::make_unique<DaemonProcessor>(std::make_unique<ThreadHostLink>(true)), options, frames, 10, 0, 1);
		harness.require(sync.started && pipelined.started, "both streams started");
		harness.expectEqual(pipelined.outputLatency, sync.outputLatency + frames, "pipelined mode reports one buffer more");

		SampleCodec codec;
		eapo::asio::findSampleCodec(ASIOSTInt32LSB, codec);
		std::vector<float> a(sync.outputs[0].size() / 4), b(pipelined.outputs[0].size() / 4);
		codec.toFloat(sync.outputs[0].data(), a.data(), static_cast<unsigned>(a.size()));
		codec.toFloat(pipelined.outputs[0].data(), b.data(), static_cast<unsigned>(b.size()));
		harness.requireEqual(b.size(), static_cast<size_t>(frames * 10), "ten periods captured");
		bool firstSilent = true, shifted = true;
		for (size_t n = 0; n < static_cast<size_t>(frames); n++)
			firstSilent = firstSilent && b[n] == 0.0f;
		for (size_t n = frames; n < b.size(); n++)
			shifted = shifted && std::fabs(b[n] - a[n - frames]) < 1e-7f;
		harness.expect(firstSilent, "the first pipelined block is silence");
		harness.expect(shifted, "every later block is the sync result one period late");
		directory.removeAll();
	}

	void testHostDeathIsGoneThenReopens()
	{
		test::TestDirectory directory(L"DaemonTests");
		const std::wstring configPath = writeConfig(directory, "Preamp: -6.0206 dB\n");
		StreamOptions options;
		options.configPath = configPath;
		options.processInput = false;
		options.deadlineUs = 2000000;
		options.mode = Mode::Sync;

		ThreadHostLink* link = new ThreadHostLink();
		std::unique_ptr<IStreamProcessor> processor(new DaemonProcessor(std::unique_ptr<eapo::asio::IHostLink>(link)));
		FakeAsioConfig config;
		config.outputChannels = 1;
		config.inputChannels = 0;
		FakeAsioDriver* fake = new FakeAsioDriver();
		fake->configure(&config);
		AsioWrapper* wrapper = new AsioWrapper(fake, testWrapperClsid, testTargetClsid, options, std::move(processor));
		fake->Release();
		asiotest::HostStub::Options hostOptions;
		hostOptions.sampleType = ASIOSTInt32LSB;
		asiotest::HostStub host(hostOptions);
		host.openChannels(0, 1);
		wrapper->init(nullptr);
		harness.requireEqual(host.createBuffers(wrapper, 32), ASE_OK, "createBuffers over the thread host");
		harness.requireEqual(wrapper->start(), ASE_OK, "start");
		fake->pump(3);
		link->killHost();
		fake->pump(3);
		wrapper->stop();
		const eapo::asio::StreamStats stats = wrapper->stats();
		harness.expectEqual(stats.gone[0], 1ull, "the host's death shows up as one Gone block");
		harness.expect(stats.blocks[0] >= 4ull, "blocks were counted up to the death");
		harness.expectEqual(wrapper->start(), ASE_HWMalfunction, "start without a reopen fails loudly");
		wrapper->disposeBuffers();
		harness.expectEqual(host.createBuffers(wrapper, 32), ASE_OK, "a reopen starts a fresh host");
		harness.expectEqual(wrapper->start(), ASE_OK, "and the stream runs again");
		fake->pump(2);
		wrapper->stop();
		harness.expectEqual(wrapper->stats().gone[0], 0ull, "the fresh stream is not Gone");
		wrapper->disposeBuffers();
		wrapper->Release();
		directory.removeAll();
	}

	void testMissingHostExecutableFailsLoudly()
	{
		StreamOptions options;
		options.daemonEndpoint = L"EAPO.ASIO.test.missing";
		options.daemonExePath = L"C:\\definitely\\not\\here\\EqualizerAPOHost.exe";
		options.readyTimeoutMs = 3000;
		options.processInput = false;
		FakeAsioConfig config;
		FakeAsioDriver* fake = new FakeAsioDriver();
		fake->configure(&config);
		AsioWrapper* wrapper = new AsioWrapper(fake, testWrapperClsid, testTargetClsid, options,
			std::make_unique<DaemonProcessor>(std::make_unique<eapo::asio::Win32HostLink>()));
		fake->Release();
		asiotest::HostStub::Options hostOptions;
		hostOptions.sampleType = ASIOSTInt32LSB;
		asiotest::HostStub host(hostOptions);
		host.openChannels(0, 2);
		wrapper->init(nullptr);
		harness.expectEqual(host.createBuffers(wrapper, 64), ASE_HWMalfunction, "a missing host executable fails createBuffers");
		char message[124] = {};
		wrapper->getErrorMessage(message);
		harness.expect(std::string(message).find("missing") != std::string::npos, std::string("the error names the missing executable: ") + message);
		harness.expect(wrapper->state() == AsioWrapper::State::Initialized, "the wrapper stays Initialized");
		wrapper->Release();
	}
}

int runDaemonTests()
{
	testDaemonMatchesInProc();
	testPipelinedShape();
	testHostDeathIsGoneThenReopens();
	testMissingHostExecutableFailsLoudly();
	harness.report();
	return 0;
}
