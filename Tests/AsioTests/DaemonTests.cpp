/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The daemon adapter over the thread host: the same DaemonProcessor,
	StreamRing and EngineHostCore the shipped wrapper and host run, with the
	host on a thread in this process. Pins that the adapter hashes exactly
	like the in-process one (transparency), the pipelined shape (one silent
	block, then everything one period late, latency reported), what a late
	pipelined block puts out, a host that dies mid-stream (Gone, sticky, and
	a clean reopen), the facts the serving loop publishes, and the loud
	failure when the real host executable is missing.
*/

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <malloc.h>

#include "asio/AsioWrapper.h"
#include "asio/DaemonProcessor.h"
#include "asio/EngineHostCore.h"
#include "asio/HostProtocol.h"
#include "asio/InProcProcessor.h"
#include "asio/SampleCodec.h"
#include "asio/StreamFacts.h"
#include "asio/ThreadHostLink.h"
#include "asio/Win32HostLink.h"
#include "devices/AsioAPOInfo.h"
#include "Tests/AsioSupport/HostStub.h"
#include "Tests/FakeAsioDriver/FakeAsio.h"
#include "Tests/FakeRegistry.h"
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

	eapo::asio::StreamFormat smallFormat();

	// Audit #348, maintainer decision: a pipelined block that is late (the
	// previous result not back yet, or the host two blocks behind so this
	// one cannot even be published) used to put out its own original audio,
	// one block earlier than everything around it. It now puts out the
	// previous block's original audio, so the one-block delay holds.
	void testPipelinedLateKeepsTheDelay()
	{
		test::TestDirectory directory(L"DaemonTests");
		const std::wstring configPath = writeConfig(directory, "Preamp: -6.0206 dB\n");
		StreamOptions options;
		options.configPath = configPath;
		options.processInput = false;
		options.mode = Mode::Pipelined;

		constexpr long frames = 64;
		ThreadHostLink* link = new ThreadHostLink();
		std::unique_ptr<IStreamProcessor> processor(new DaemonProcessor(std::unique_ptr<eapo::asio::IHostLink>(link)));
		FakeAsioConfig config;
		config.sampleType = ASIOSTInt32LSB;
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
		harness.requireEqual(host.createBuffers(wrapper, frames), ASE_OK, "createBuffers over the thread host");
		harness.requireEqual(wrapper->start(), ASE_OK, "start");

		// One period at a time with room for the host in between, so only the
		// held periods can be late.
		const auto pumpServed = [&](long periods) {
			for (long p = 0; p < periods; p++)
			{
				fake->pump(1);
				Sleep(50);
			}
		};
		pumpServed(6);           // periods 0-5: silence, then each previous block processed
		link->holdHost(true);    // the host has served seq 6 (period 5) and keeps the next one
		fake->pump(1);           // period 6: seq 7 published (and held), seq 6 is back: processed
		fake->pump(1);           // period 7: seq 8 published, seq 7 held: Late
		fake->pump(1);           // period 8: seq 9 cannot be published (seq 7 not done): Late
		link->holdHost(false);
		Sleep(100);
		pumpServed(3);           // periods 9-11
		wrapper->stop();
		const eapo::asio::StreamStats stats = wrapper->stats();
		wrapper->disposeBuffers();

		const unsigned char* data = nullptr;
		unsigned long bytes = 0;
		fake->capturedOutput(0, &data, &bytes);
		SampleCodec codec;
		eapo::asio::findSampleCodec(ASIOSTInt32LSB, codec);
		std::vector<float> out(bytes / 4);
		codec.toFloat(data, out.data(), static_cast<unsigned>(out.size()));
		harness.requireEqual(out.size(), static_cast<size_t>(frames * 12), "twelve periods captured");
		harness.expectEqual(stats.late[0], 2ull, "the two held periods were late");
		harness.expectEqual(stats.gone[0], 0ull, "and nothing was Gone");

		// What the host wrote in period p, as the wrapper saw it: once
		// through the target's sample format.
		const auto dry = [&](long period) {
			std::vector<float> samples(static_cast<size_t>(frames));
			std::vector<unsigned char> quantized(static_cast<size_t>(frames) * 4);
			for (long n = 0; n < frames; n++)
				samples[static_cast<size_t>(n)] = host.outputSample(0, static_cast<uint64_t>(period) * frames + n);
			codec.fromFloat(samples.data(), quantized.data(), static_cast<unsigned>(frames));
			codec.toFloat(quantized.data(), samples.data(), static_cast<unsigned>(frames));
			return samples;
		};
		const auto periodIs = [&](long period, const std::vector<float>& expected, float gain) {
			for (long n = 0; n < frames; n++)
			{
				if (std::fabs(out[static_cast<size_t>(period * frames + n)] - expected[static_cast<size_t>(n)] * gain) > 1e-5f)
					return false;
			}
			return true;
		};
		constexpr float half = 0.5f;   // -6.0206 dB
		harness.expect(periodIs(0, std::vector<float>(frames, 0.0f), 1.0f), "the first period is silence");
		harness.expect(periodIs(5, dry(4), half), "before the hold, a period carries the previous one processed");
		harness.expect(periodIs(6, dry(5), half), "the period published as the host stopped still gets its predecessor processed");
		harness.expect(periodIs(7, dry(6), 1.0f), "a late period puts out the previous period's original audio");
		harness.expectFalse(periodIs(7, dry(7), 1.0f), "not its own");
		harness.expect(periodIs(8, dry(7), 1.0f), "a period dropped for a full ring does the same");
		harness.expectFalse(periodIs(8, dry(8), 1.0f), "not its own either");
		// Period 8 was never published, so the next result out is period 7's
		// (the one seq 8 carried), then the timeline runs on.
		harness.expect(periodIs(9, dry(7), half), "after the hold the host's results resume in order");
		harness.expect(periodIs(10, dry(9), half), "and the one-period delay is back");
		harness.expect(periodIs(11, dry(10), half), "and stays");
		wrapper->Release();
		directory.removeAll();
	}

	// The serving loop writes the facts the device record reads: the one
	// attach procedure the host runs, over a heap ring, publishing into a
	// fake registry that AsioAPOInfo then reads (audit #348 C7).
	void testServeLoopPublishesTheFacts()
	{
		test::TestDirectory directory(L"DaemonTests");
		const std::wstring configPath = writeConfig(directory, "Preamp: 0 dB\n");
		const wchar_t* const targetClsid = L"{6D241B5E-CF73-4043-A85F-EF11D4670955}";
		eapo::asio::StreamFormat format = smallFormat();
		format.sampleRate = 44100.0;
		format.channels[1] = 4;
		wcsncpy_s(format.deviceName, L"Topping USB Audio Device", _TRUNCATE);
		wcsncpy_s(format.deviceGuid, targetClsid, _TRUNCATE);

		const uint32_t bytes = eapo::ipc::RingGeometry::totalBytes(format);
		void* region = _aligned_malloc(bytes, eapo::ipc::ringAlignment);
		harness.require(region != nullptr, "the ring is allocated");
		if (region == nullptr)
			return;
		std::memset(region, 0, bytes);
		HANDLE events[eapo::asio::RingEvents::count] = {};
		for (unsigned i = 0; i < eapo::asio::RingEvents::count; i++)
			events[i] = CreateEventW(nullptr, eapo::asio::RingEvents::table[i].manualReset ? TRUE : FALSE, FALSE, nullptr);
		HANDLE hostGone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE producerGone = CreateEventW(nullptr, TRUE, FALSE, nullptr);

		test::FakeRegistry registry;
		eapo::asio::ServeOptions serve;
		serve.configPath = configPath;
		serve.proAudio = false;
		serve.idleWaitMs = 100;
		serve.registry = &registry;
		// Started before the producer formats the header, as the real host
		// is: the attach procedure waits for Announced.
		std::thread host([&] {
			eapo::asio::EngineHostCore::attachAndServe(region, bytes, eapo::asio::RingEvents::toSync(events, producerGone), serve, 4242);
			SetEvent(hostGone);
		});
		Sleep(20);
		{
			eapo::ipc::RingProducer producer(region, format, GetCurrentProcessId(), eapo::asio::RingEvents::toSync(events, hostGone));
			harness.expect(producer.waitReady(20000) == eapo::ipc::RingState::Ready, "the serving loop attaches and becomes Ready");
			harness.expectEqual(producer.consumerPid(), 4242u, "with the pid it was given");
			producer.close();
		}
		host.join();

		eapo::asio::StreamShape shape;
		harness.require(eapo::asio::StreamFacts::read(registry, targetClsid, shape), "the loop published the stream's facts");
		harness.expectEqual(shape.sampleRate, 44100ul, "the sample rate");
		harness.expectEqual(shape.channels[0], 2ul, "the output channels");
		harness.expectEqual(shape.channels[1], 4ul, "the input channels");
		eapo::asio::AsioTarget target;
		target.name = L"Topping USB Audio Device";
		target.clsid = targetClsid;
		AsioAPOInfo playback(target, false, registry);
		AsioAPOInfo capture(target, true, registry);
		harness.expectEqual(playback.getSampleRate(), 44100u, "the playback record reads the rate the host wrote");
		harness.expectEqual(playback.getChannelCount(), 2u, "and its channel count");
		harness.expectEqual(capture.getChannelCount(), 4u, "the capture record reads its own");

		for (HANDLE event : events)
			CloseHandle(event);
		CloseHandle(hostGone);
		CloseHandle(producerGone);
		_aligned_free(region);
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

	// Audit #348 TD-46: the 32-bit wrapper ships alone in the x86 folder and
	// looked for the host beside itself, so it could not start one.
	void testHostExecutableFallsBackToTheParentFolder()
	{
		test::TestDirectory directory(L"DaemonTests-host");
		const std::wstring x86 = directory.path() + L"\\x86";
		CreateDirectoryW(x86.c_str(), nullptr);
		const std::wstring parentHost = directory.path() + L"\\EqualizerAPOHost.exe";
		const std::wstring besideHost = x86 + L"\\EqualizerAPOHost.exe";
		HANDLE file = CreateFileW(parentHost.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		CloseHandle(file);

		StreamOptions options;
		harness.expect(eapo::asio::Win32HostLink::hostExecutable(options, x86) == parentHost,
			"with no host beside the module, the parent folder's host is the one to start");
		file = CreateFileW(besideHost.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		CloseHandle(file);
		harness.expect(eapo::asio::Win32HostLink::hostExecutable(options, x86) == besideHost,
			"a host beside the module comes first");
		options.daemonExePath = L"C:\\elsewhere\\EqualizerAPOHost.exe";
		harness.expect(eapo::asio::Win32HostLink::hostExecutable(options, x86) == options.daemonExePath,
			"and a path in the options comes before both");

		DeleteFileW(besideHost.c_str());
		DeleteFileW(parentHost.c_str());
		RemoveDirectoryW(x86.c_str());
		directory.removeAll();
	}

	// A test-owned server stands in for another program on the control pipe.
	// replyNever makes it read the request and then say nothing.
	class PipeSquatter
	{
	public:
		explicit PipeSquatter(const std::wstring& endpoint)
			: pipe_(CreateNamedPipeW(eapo::asio::HostNames::pipe(endpoint).c_str(), PIPE_ACCESS_DUPLEX,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
				sizeof(eapo::asio::HostOpenReply), sizeof(eapo::asio::HostOpenRequest), 0, nullptr)),
			release_(CreateEventW(nullptr, TRUE, FALSE, nullptr))
		{
			thread_ = std::thread([this] {
				const bool connected = ConnectNamedPipe(pipe_, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
				if (connected)
				{
					eapo::asio::HostOpenRequest request;
					DWORD read = 0;
					requestRead_ = ReadFile(pipe_, &request, sizeof(request), &read, nullptr) && read == sizeof(request);
					WaitForSingleObject(release_, 30000);
				}
				done_ = true;
			});
		}

		~PipeSquatter()
		{
			SetEvent(release_);
			while (!done_)
			{
				CancelSynchronousIo(thread_.native_handle());
				Sleep(10);
			}
			thread_.join();
			CloseHandle(pipe_);
			CloseHandle(release_);
		}

		PipeSquatter(const PipeSquatter&) = delete;
		PipeSquatter& operator=(const PipeSquatter&) = delete;

		bool created() const
		{
			return pipe_ != INVALID_HANDLE_VALUE;
		}

		bool requestRead() const
		{
			return requestRead_;
		}

	private:
		HANDLE pipe_;
		HANDLE release_;
		std::thread thread_;
		std::atomic<bool> done_ = false;
		std::atomic<bool> requestRead_ = false;
	};

	eapo::asio::StreamFormat smallFormat()
	{
		eapo::asio::StreamFormat format;
		format.sampleRate = 48000.0;
		format.frames = 64;
		format.channels[0] = 2;
		format.channels[1] = 2;
		return format;
	}

	void testAnotherProgramOnTheControlPipeIsRefused()
	{
		const std::wstring endpoint = L"EAPO.ASIO.test.squat." + std::to_wstring(GetCurrentProcessId());
		PipeSquatter squatter(endpoint);
		harness.require(squatter.created(), "the stand-in server holds the control pipe");

		StreamOptions options;
		options.daemonEndpoint = endpoint;
		options.daemonExePath = L"C:\\definitely\\not\\here\\EqualizerAPOHost.exe";
		options.readyTimeoutMs = 3000;
		eapo::asio::Win32HostLink link;
		eapo::asio::HostSession session;
		std::string error;
		const bool opened = link.open(smallFormat(), options, session, error);
		harness.expectFalse(opened, "the link does not use a pipe served by a program other than the host");
		harness.expect(error.find("held by another program") != std::string::npos, "and says so: " + error);
		harness.expectFalse(squatter.requestRead(), "the stand-in never receives the stream request");
		link.close(session);
	}

	void testASilentServerTimesOut()
	{
		const std::wstring endpoint = L"EAPO.ASIO.test.silent." + std::to_wstring(GetCurrentProcessId());
		PipeSquatter squatter(endpoint);
		harness.require(squatter.created(), "the silent server holds the control pipe");

		wchar_t self[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, self, MAX_PATH);
		StreamOptions options;
		options.daemonEndpoint = endpoint;
		// The test process is the server here, so it passes the identity check
		// and only the missing reply is under test.
		options.daemonExePath = self;
		options.readyTimeoutMs = 200;
		eapo::asio::Win32HostLink link;
		eapo::asio::HostSession session;
		std::string error;
		const auto started = std::chrono::steady_clock::now();
		const bool opened = link.open(smallFormat(), options, session, error);
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
		harness.expectFalse(opened, "a server that never replies does not hang the open call");
		harness.expect(error.find("in time") != std::string::npos, "the error says the host did not answer in time: " + error);
		harness.expect(elapsed < 10000, "and the call gives up within seconds (" + std::to_string(elapsed) + " ms)");
		harness.expect(squatter.requestRead(), "the request itself was delivered");
		link.close(session);
	}
}

int runDaemonTests()
{
	testDaemonMatchesInProc();
	testPipelinedShape();
	testPipelinedLateKeepsTheDelay();
	testHostDeathIsGoneThenReopens();
	testServeLoopPublishesTheFacts();
	testMissingHostExecutableFailsLoudly();
	testHostExecutableFallsBackToTheParentFolder();
	testAnotherProgramOnTheControlPipeIsRefused();
	testASilentServerTimesOut();
	harness.report();
	return 0;
}
