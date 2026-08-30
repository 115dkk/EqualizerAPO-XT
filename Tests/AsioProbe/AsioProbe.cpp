/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The ASIO probe: a console host that stands in for a DAW. It builds a
	wrapper over a target driver, runs a stream, and compares what reached
	the "hardware" with what the engine produces when fed the same signal
	directly. The two hashes agreeing is the transparency proof for whichever
	processor adapter is under test; the first-period hash agreeing is the
	readiness-barrier proof.

	Targets:   fake (the statically linked FakeAsioDriver),
	           dll:<path> (FakeAsioDriver.dll through DllGetClassObject),
	           clsid:{...} (a real driver through CoCreateInstance; no pump,
	           the driver's own clock runs for --seconds)
	Wrappers:  static (AsioWrapper linked in, processor chosen by --processor),
	           dll:<path> (EqualizerAPOAsio.dll through EapoAsioCreateWrapper)
	Processors: inproc (two FilterEngines in this process), passthrough,
	           daemon-thread (the daemon adapter over the engine host running
	           on a thread in this process), daemon (the daemon adapter over
	           EqualizerAPOHost.exe, reached or started on --endpoint)

	Exit codes: 0 ok, 1 usage, 2 the stream could not be opened or started,
	3 a hash mismatch, 4 more late blocks than --max-late allows.
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "asio/AsioSdk.h"
#include "asio/AsioWrapper.h"
#include "asio/DaemonProcessor.h"
#include "asio/InProcProcessor.h"
#include "asio/ThreadHostLink.h"
#include "asio/Win32HostLink.h"
#include "asio/SampleCodec.h"
#include "asio/WasapiExclusiveTarget.h"
#include "engine/FilterEngine.h"
#include "services/logging/Logging.h"
#include "Tests/AsioSupport/HostStub.h"
#include "Tests/AsioSupport/Sha256.h"
#include "Tests/FakeAsioDriver/FakeAsio.h"

using eapo::asio::AsioWrapper;
using eapo::asio::Direction;
using eapo::asio::IStreamProcessor;
using eapo::asio::Mode;
using eapo::asio::SampleCodec;
using eapo::asio::StreamOptions;
using eapo::asio::StreamStats;

namespace
{
	// {5C2B9E10-8D4F-4A7B-B3E6-0F1A2C3D4E5F}, shared with the DLL's probe entry.
	constexpr GUID probeWrapperClsid = {0x5c2b9e10, 0x8d4f, 0x4a7b, {0xb3, 0xe6, 0x0f, 0x1a, 0x2c, 0x3d, 0x4e, 0x5f}};

	struct Arguments
	{
		std::wstring target = L"fake";
		std::wstring wrapper = L"static";
		std::wstring processor = L"inproc";
		std::wstring config;
		std::wstring mode = L"sync";
		long frames = 64;
		double rate = 48000.0;
		bool rateGiven = false;       // a real target is asked for --rate only when it was given
		long periods = 200;
		long inputs = 2;
		long outputs = 2;
		long sampleType = ASIOSTInt32LSB;
		unsigned seed = 1;
		unsigned hostSeed = 7;
		bool processInput = true;
		bool processOutput = true;
		bool callOutputReady = false;
		double seconds = 10.0;
		bool tone = false;
		double sineHz = 0.0;
		std::string expectSha;
		std::string expectFirstSha;
		std::string expectInputSha;
		long maxLate = 0;
		bool reference = true;
		std::wstring daemonExe;
		std::wstring endpoint = L"EAPO.ASIO.probe";
		uint32_t deadlineUs = 0;
	};

	std::string narrow(const std::wstring& text)
	{
		std::string result;
		result.reserve(text.size());
		for (wchar_t c : text)
			result.push_back(c < 128 ? static_cast<char>(c) : '?');
		return result;
	}

	void usage()
	{
		std::fputs(
			"AsioProbe --target fake|dll:<FakeAsioDriver.dll>|clsid:{...}|wasapi:{...}[,{...}] --wrapper static|dll:<EqualizerAPOAsio.dll>\n"
			"          --processor inproc|passthrough|daemon-thread|daemon --config <config.txt> [--mode sync|pipelined]\n"
			"          [--daemon <EqualizerAPOHost.exe>] [--endpoint <name>] [--deadline-us N]\n"
			"          [--frames 64] [--rate 48000] [--periods 200] [--channels in,out] [--sample-type int16|int24|int32|float32]\n"
			"          [--seed N] [--host-seed N] [--no-input] [--no-output] [--output-ready]\n"
			"          [--expect-sha256 hex] [--expect-first-sha256 hex] [--expect-input-sha256 hex] [--max-late N] [--no-reference]\n"
			"          [--seconds 10] [--tone] [--sine Hz]   (real driver / wasapi:{playback guid}[,{recording guid}] only)\n", stderr);
	}

	bool parse(int argc, wchar_t** argv, Arguments& a)
	{
		for (int i = 1; i < argc; i++)
		{
			const std::wstring key = argv[i];
			auto value = [&](std::wstring& out) {
				if (i + 1 >= argc)
					return false;
				out = argv[++i];
				return true;
			};
			std::wstring v;
			if (key == L"--target" && value(v)) a.target = v;
			else if (key == L"--wrapper" && value(v)) a.wrapper = v;
			else if (key == L"--processor" && value(v)) a.processor = v;
			else if (key == L"--config" && value(v)) a.config = v;
			else if (key == L"--mode" && value(v)) a.mode = v;
			else if (key == L"--frames" && value(v)) a.frames = std::wcstol(v.c_str(), nullptr, 10);
			else if (key == L"--rate" && value(v))
			{
				a.rate = std::wcstod(v.c_str(), nullptr);
				a.rateGiven = true;
			}
			else if (key == L"--periods" && value(v)) a.periods = std::wcstol(v.c_str(), nullptr, 10);
			else if (key == L"--seconds" && value(v)) a.seconds = std::wcstod(v.c_str(), nullptr);
			else if (key == L"--seed" && value(v)) a.seed = static_cast<unsigned>(std::wcstoul(v.c_str(), nullptr, 10));
			else if (key == L"--host-seed" && value(v)) a.hostSeed = static_cast<unsigned>(std::wcstoul(v.c_str(), nullptr, 10));
			else if (key == L"--max-late" && value(v)) a.maxLate = std::wcstol(v.c_str(), nullptr, 10);
			else if (key == L"--daemon" && value(v)) a.daemonExe = v;
			else if (key == L"--endpoint" && value(v)) a.endpoint = v;
			else if (key == L"--deadline-us" && value(v)) a.deadlineUs = static_cast<uint32_t>(std::wcstoul(v.c_str(), nullptr, 10));
			else if (key == L"--channels" && value(v))
			{
				const size_t comma = v.find(L',');
				if (comma == std::wstring::npos)
					return false;
				a.inputs = std::wcstol(v.substr(0, comma).c_str(), nullptr, 10);
				a.outputs = std::wcstol(v.substr(comma + 1).c_str(), nullptr, 10);
			}
			else if (key == L"--sample-type" && value(v))
			{
				if (v == L"int16") a.sampleType = ASIOSTInt16LSB;
				else if (v == L"int24") a.sampleType = ASIOSTInt24LSB;
				else if (v == L"int32") a.sampleType = ASIOSTInt32LSB;
				else if (v == L"float32") a.sampleType = ASIOSTFloat32LSB;
				else return false;
			}
			else if (key == L"--expect-sha256" && value(v)) a.expectSha = narrow(v);
			else if (key == L"--expect-first-sha256" && value(v)) a.expectFirstSha = narrow(v);
			else if (key == L"--expect-input-sha256" && value(v)) a.expectInputSha = narrow(v);
			else if (key == L"--no-input") a.processInput = false;
			else if (key == L"--no-output") a.processOutput = false;
			else if (key == L"--output-ready") a.callOutputReady = true;
			else if (key == L"--no-reference") a.reference = false;
			else if (key == L"--tone") a.tone = true;
			else if (key == L"--sine" && value(v)) a.sineHz = std::wcstod(v.c_str(), nullptr);
			else return false;
		}
		return a.frames > 0 && a.periods > 0 && a.rate > 0.0;
	}

	struct Module
	{
		HMODULE handle = nullptr;
		~Module()
		{
			// Deliberately not freed: COM objects created from the DLL may
			// still be releasing during teardown, and the process is exiting.
		}
	};

	IASIO* loadFromDll(const std::wstring& path, const CLSID& clsid, Module& module)
	{
		module.handle = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (module.handle == nullptr)
		{
			std::fwprintf(stderr, L"AsioProbe: cannot load %s (error %lu)\n", path.c_str(), GetLastError());
			return nullptr;
		}
		typedef HRESULT (STDAPICALLTYPE* GetClassObject)(REFCLSID, REFIID, void**);
		GetClassObject getClassObject = reinterpret_cast<GetClassObject>(GetProcAddress(module.handle, "DllGetClassObject"));
		if (getClassObject == nullptr)
		{
			std::fwprintf(stderr, L"AsioProbe: %s exports no DllGetClassObject\n", path.c_str());
			return nullptr;
		}
		IClassFactory* factory = nullptr;
		HRESULT hr = getClassObject(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory));
		if (FAILED(hr) || factory == nullptr)
		{
			std::fwprintf(stderr, L"AsioProbe: DllGetClassObject failed with 0x%08x\n", static_cast<unsigned>(hr));
			return nullptr;
		}
		IASIO* driver = nullptr;
		hr = factory->CreateInstance(nullptr, clsid, reinterpret_cast<void**>(&driver));
		factory->Release();
		if (FAILED(hr) || driver == nullptr)
		{
			std::fwprintf(stderr, L"AsioProbe: CreateInstance failed with 0x%08x\n", static_cast<unsigned>(hr));
			return nullptr;
		}
		return driver;
	}

	IASIO* loadRealDriver(const std::wstring& clsidText)
	{
		CLSID clsid;
		if (FAILED(CLSIDFromString(clsidText.c_str(), &clsid)))
		{
			std::fwprintf(stderr, L"AsioProbe: %s is not a CLSID\n", clsidText.c_str());
			return nullptr;
		}
		IASIO* driver = nullptr;
		const HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(&driver));
		if (FAILED(hr) || driver == nullptr)
		{
			std::fwprintf(stderr, L"AsioProbe: CoCreateInstance(%s) failed with 0x%08x\n", clsidText.c_str(), static_cast<unsigned>(hr));
			return nullptr;
		}
		return driver;
	}

	IASIO* wrapThroughDll(const std::wstring& path, IASIO* target, const std::wstring& targetClsid, const Arguments& a, Module& module)
	{
		module.handle = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (module.handle == nullptr)
		{
			std::fwprintf(stderr, L"AsioProbe: cannot load %s (error %lu)\n", path.c_str(), GetLastError());
			return nullptr;
		}
		typedef HRESULT (__stdcall* CreateWrapper)(IASIO*, const wchar_t*, const wchar_t*, const wchar_t*, IASIO**);
		CreateWrapper create = reinterpret_cast<CreateWrapper>(GetProcAddress(module.handle, "EapoAsioCreateWrapper"));
		if (create == nullptr)
		{
			std::fwprintf(stderr, L"AsioProbe: %s exports no EapoAsioCreateWrapper\n", path.c_str());
			return nullptr;
		}
		std::wstring options = L"output=" + std::wstring(a.processOutput ? L"1" : L"0")
			+ L";input=" + std::wstring(a.processInput ? L"1" : L"0")
			+ L";mode=" + a.mode + L";deadline=" + std::to_wstring(a.deadlineUs)
			+ L";endpoint=" + a.endpoint + L";daemon=" + a.daemonExe + L";config=" + a.config;
		IASIO* wrapper = nullptr;
		const HRESULT hr = create(target, targetClsid.c_str(), options.c_str(), a.processor.c_str(), &wrapper);
		if (FAILED(hr) || wrapper == nullptr)
		{
			std::fwprintf(stderr, L"AsioProbe: EapoAsioCreateWrapper failed with 0x%08x\n", static_cast<unsigned>(hr));
			return nullptr;
		}
		return wrapper;
	}

	// Runs the engine directly over the same quantized signal the wrapper
	// saw, one period at a time, and returns the per-channel byte streams
	// in the layout the fake driver records them in.
	bool computeReference(const Arguments& a, const SampleCodec& codec, const asiotest::HostStub& host, bool capture,
		std::vector<std::vector<unsigned char>>& records, std::vector<unsigned char>& firstPeriod)
	{
		const long channels = capture ? a.inputs : a.outputs;
		records.assign(static_cast<size_t>(channels), std::vector<unsigned char>());
		firstPeriod.clear();
		if (channels == 0)
			return true;

		std::unique_ptr<FilterEngine> engine;
		const bool filtering = a.processor != L"passthrough" && (capture ? a.processInput : a.processOutput);
		// The pipelined adapter hands out one block of silence first and the
		// processed stream one period late; the last period never leaves.
		const bool pipelined = filtering && a.mode == L"pipelined";
		if (pipelined)
		{
			std::vector<unsigned char> silence(static_cast<size_t>(codec.bytesPerSample) * a.frames, 0);
			std::vector<float> zeros(static_cast<size_t>(a.frames), 0.0f);
			codec.fromFloat(zeros.data(), silence.data(), static_cast<unsigned>(a.frames));
			for (long c = 0; c < channels; c++)
				records[static_cast<size_t>(c)].insert(records[static_cast<size_t>(c)].end(), silence.begin(), silence.end());
		}
		if (filtering)
		{
			engine = std::make_unique<FilterEngine>();
			EngineSetup setup;
			setup.sampleRate = static_cast<float>(a.rate);
			setup.inputChannelCount = static_cast<unsigned>(channels);
			setup.realChannelCount = static_cast<unsigned>(channels);
			setup.outputChannelCount = static_cast<unsigned>(channels);
			setup.channelMask = 0;
			setup.maxFrameCount = static_cast<unsigned>(a.frames);
			setup.customPath = a.config;
			setup.capture = capture;
			setup.deviceName = L"FakeAsio";
			setup.connectionName = L"ASIO";
			setup.deviceGuid = L"{B7E3A9F4-52C1-4D0B-8A6E-1F9C3D5E7B21}";
			engine->initialize(setup);
		}

		std::vector<float> storage(static_cast<size_t>(channels) * a.frames);
		std::vector<float*> planes(static_cast<size_t>(channels));
		for (long c = 0; c < channels; c++)
			planes[static_cast<size_t>(c)] = storage.data() + static_cast<size_t>(c) * a.frames;
		std::vector<unsigned char> bytes(static_cast<size_t>(codec.bytesPerSample) * a.frames);
		const long referencePeriods = pipelined ? a.periods - 1 : a.periods;
		for (long p = 0; p < referencePeriods; p++)
		{
			for (long c = 0; c < channels; c++)
			{
				float* plane = planes[static_cast<size_t>(c)];
				for (long n = 0; n < a.frames; n++)
				{
					const uint64_t index = static_cast<uint64_t>(p) * a.frames + n;
					plane[n] = capture ? FakeAsioDriver::generatorSample(a.seed, c, index) : host.outputSample(c, index);
				}
				// The wrapper only ever sees samples that went through the
				// target's sample format once.
				codec.fromFloat(plane, bytes.data(), static_cast<unsigned>(a.frames));
				codec.toFloat(bytes.data(), plane, static_cast<unsigned>(a.frames));
			}
			if (engine != nullptr)
				engine->process(planes.data(), planes.data(), static_cast<unsigned>(a.frames));
			for (long c = 0; c < channels; c++)
			{
				codec.fromFloat(planes[static_cast<size_t>(c)], bytes.data(), static_cast<unsigned>(a.frames));
				records[static_cast<size_t>(c)].insert(records[static_cast<size_t>(c)].end(), bytes.begin(), bytes.end());
				if (p == 0)
					firstPeriod.insert(firstPeriod.end(), bytes.begin(), bytes.end());
			}
		}
		return true;
	}

	std::string hashOf(const std::vector<std::vector<unsigned char>>& records)
	{
		std::vector<unsigned char> all;
		for (const std::vector<unsigned char>& record : records)
			all.insert(all.end(), record.begin(), record.end());
		return asiotest::sha256Hex(all);
	}

	void printProfile(const AsioWrapper* staticWrapper)
	{
		const eapo::asio::DaemonProcessor* daemon = staticWrapper != nullptr
			? dynamic_cast<const eapo::asio::DaemonProcessor*>(staticWrapper->processor()) : nullptr;
		if (daemon == nullptr)
			return;
		const eapo::asio::DaemonProcessor::HandoffProfile& p = daemon->profile();
		std::printf("handoff deadline=%u us; max dispatch=%u service=%u return=%u us over %llu completed\n",
			daemon->deadlineUs(), p.maxDispatchUs, p.maxServiceUs, p.maxReturnUs, static_cast<unsigned long long>(p.completed));
		std::printf("round-trip <100us=%llu <200=%llu <500=%llu <1000=%llu <2000=%llu >=2000=%llu\n",
			static_cast<unsigned long long>(p.roundTripBuckets[0]), static_cast<unsigned long long>(p.roundTripBuckets[1]),
			static_cast<unsigned long long>(p.roundTripBuckets[2]), static_cast<unsigned long long>(p.roundTripBuckets[3]),
			static_cast<unsigned long long>(p.roundTripBuckets[4]), static_cast<unsigned long long>(p.roundTripBuckets[5]));
	}

	int runFakeStream(const Arguments& a, IASIO* wrapper, IFakeAsioControl* control, AsioWrapper* staticWrapper)
	{
		SampleCodec codec;
		eapo::asio::findSampleCodec(a.sampleType, codec);
		asiotest::HostStub::Options hostOptions;
		hostOptions.sampleType = a.sampleType;
		hostOptions.outputSeed = a.hostSeed;
		hostOptions.callOutputReady = a.callOutputReady;
		asiotest::HostStub host(hostOptions);
		host.openChannels(a.inputs, a.outputs);

		if (wrapper->init(nullptr) == ASIOFalse)
		{
			char message[124] = {};
			wrapper->getErrorMessage(message);
			std::fprintf(stderr, "AsioProbe: init failed: %s\n", message);
			return 2;
		}
		ASIOError error = host.createBuffers(wrapper, a.frames);
		if (error != ASE_OK)
		{
			char message[124] = {};
			wrapper->getErrorMessage(message);
			std::fprintf(stderr, "AsioProbe: createBuffers failed with %ld: %s\n", error, message);
			return 2;
		}
		long inputLatency = 0, outputLatency = 0;
		wrapper->getLatencies(&inputLatency, &outputLatency);
		error = wrapper->start();
		if (error != ASE_OK)
		{
			char message[124] = {};
			wrapper->getErrorMessage(message);
			std::fprintf(stderr, "AsioProbe: start failed with %ld: %s\n", error, message);
			return 2;
		}
		control->pump(a.periods);
		wrapper->stop();

		StreamStats stats;
		if (staticWrapper != nullptr)
			stats = staticWrapper->stats();
		printProfile(staticWrapper);
		wrapper->disposeBuffers();

		std::vector<std::vector<unsigned char>> outputs(static_cast<size_t>(a.outputs));
		std::vector<unsigned char> firstPeriod;
		for (long c = 0; c < a.outputs; c++)
		{
			const unsigned char* data = nullptr;
			unsigned long bytes = 0;
			control->capturedOutput(c, &data, &bytes);
			outputs[static_cast<size_t>(c)].assign(data, data + bytes);
			const size_t periodBytes = static_cast<size_t>(codec.bytesPerSample) * a.frames;
			const size_t skip = a.mode == L"pipelined" && a.processor != L"passthrough" ? periodBytes : 0;
			if (bytes >= skip + periodBytes)
				firstPeriod.insert(firstPeriod.end(), data + skip, data + skip + periodBytes);
		}
		std::vector<std::vector<unsigned char>> inputs(static_cast<size_t>(a.inputs));
		for (long c = 0; c < a.inputs; c++)
			inputs[static_cast<size_t>(c)] = host.inputRecord(static_cast<size_t>(c));

		const std::string outputSha = hashOf(outputs);
		const std::string firstSha = asiotest::sha256Hex(firstPeriod);
		const std::string inputSha = hashOf(inputs);
		std::printf("output-sha256 %s\nfirst-period-sha256 %s\ninput-sha256 %s\n", outputSha.c_str(), firstSha.c_str(), inputSha.c_str());
		std::printf("latency input=%ld output=%ld frames\n", inputLatency, outputLatency);
		if (staticWrapper != nullptr)
		{
			std::printf("blocks out=%llu in=%llu late out=%llu in=%llu gone out=%llu in=%llu stale=%u\n",
				static_cast<unsigned long long>(stats.blocks[0]), static_cast<unsigned long long>(stats.blocks[1]),
				static_cast<unsigned long long>(stats.late[0]), static_cast<unsigned long long>(stats.late[1]),
				static_cast<unsigned long long>(stats.gone[0]), static_cast<unsigned long long>(stats.gone[1]), stats.staleBlocks);
			std::printf("process-us max out=%u in=%u\n", stats.maxProcessUs[0], stats.maxProcessUs[1]);
		}

		int result = 0;
		if (a.reference)
		{
			std::vector<std::vector<unsigned char>> referenceOutputs, referenceInputs;
			std::vector<unsigned char> referenceFirst, unused;
			computeReference(a, codec, host, false, referenceOutputs, referenceFirst);
			computeReference(a, codec, host, true, referenceInputs, unused);
			const std::string referenceOutputSha = hashOf(referenceOutputs);
			const std::string referenceFirstSha = asiotest::sha256Hex(referenceFirst);
			const std::string referenceInputSha = hashOf(referenceInputs);
			std::printf("reference-output-sha256 %s\nreference-first-period-sha256 %s\nreference-input-sha256 %s\n",
				referenceOutputSha.c_str(), referenceFirstSha.c_str(), referenceInputSha.c_str());
			if (outputSha != referenceOutputSha)
			{
				std::fputs("AsioProbe: the output that reached the target differs from the engine's direct output\n", stderr);
				result = 3;
			}
			if (firstSha != referenceFirstSha)
			{
				std::fputs("AsioProbe: the first period is not the engine's first period (readiness barrier)\n", stderr);
				result = 3;
			}
			if (inputSha != referenceInputSha)
			{
				std::fputs("AsioProbe: the input the host received differs from the engine's direct capture output\n", stderr);
				result = 3;
			}
		}
		if (!a.expectSha.empty() && outputSha != a.expectSha)
		{
			std::fputs("AsioProbe: output hash differs from --expect-sha256\n", stderr);
			result = 3;
		}
		if (!a.expectFirstSha.empty() && firstSha != a.expectFirstSha)
		{
			std::fputs("AsioProbe: first-period hash differs from --expect-first-sha256\n", stderr);
			result = 3;
		}
		if (!a.expectInputSha.empty() && inputSha != a.expectInputSha)
		{
			std::fputs("AsioProbe: input hash differs from --expect-input-sha256\n", stderr);
			result = 3;
		}
		if (staticWrapper != nullptr && static_cast<long>(stats.late[0] + stats.late[1]) > a.maxLate)
		{
			std::fprintf(stderr, "AsioProbe: %llu late blocks exceed --max-late %ld\n",
				static_cast<unsigned long long>(stats.late[0] + stats.late[1]), a.maxLate);
			result = result == 0 ? 4 : result;
		}
		return result;
	}

	int runRealStream(const Arguments& a, IASIO* wrapper, AsioWrapper* staticWrapper)
	{
		long inputs = 0, outputs = 0;
		asiotest::HostStub::Options hostOptions;
		hostOptions.outputSeed = a.tone || a.sineHz > 0.0 ? a.hostSeed : 0;
		hostOptions.outputScale = a.sineHz > 0.0 ? 0.5f : 0.05f;
		hostOptions.sineHz = a.sineHz;
		hostOptions.callOutputReady = a.callOutputReady;
		hostOptions.proAudioCallback = true;
		if (wrapper->init(nullptr) == ASIOFalse)
		{
			char message[124] = {};
			wrapper->getErrorMessage(message);
			std::fprintf(stderr, "AsioProbe: init failed: %s\n", message);
			return 2;
		}
		wrapper->getChannels(&inputs, &outputs);
		// A DAW sets the rate before it asks for channel types or buffers; so
		// does the probe when --rate was given (the sample type may follow).
		if (a.rateGiven)
		{
			double current = 0.0;
			wrapper->getSampleRate(&current);
			if (current != a.rate)
			{
				if (wrapper->canSampleRate(a.rate) != ASE_OK || wrapper->setSampleRate(a.rate) != ASE_OK)
					std::fprintf(stderr, "AsioProbe: the target refused %.0f Hz; staying at %.0f\n", a.rate, current);
			}
		}
		ASIOChannelInfo info = {};
		info.channel = 0;
		info.isInput = outputs > 0 ? ASIOFalse : ASIOTrue;
		wrapper->getChannelInfo(&info);
		hostOptions.sampleType = info.type;
		long minSize = 0, maxSize = 0, preferred = 0, granularity = 0;
		wrapper->getBufferSize(&minSize, &maxSize, &preferred, &granularity);
		const long frames = a.frames > 0 && a.frames >= minSize && a.frames <= maxSize ? a.frames : preferred;
		double rate = 0.0;
		wrapper->getSampleRate(&rate);
		std::printf("target channels in=%ld out=%ld buffer=%ld (min %ld max %ld preferred %ld) rate=%.0f type=%ld\n",
			inputs, outputs, frames, minSize, maxSize, preferred, rate, info.type);
		hostOptions.sampleRate = rate > 0.0 ? rate : 48000.0;

		asiotest::HostStub host(hostOptions);
		host.openChannels(inputs, outputs);
		ASIOError error = host.createBuffers(wrapper, frames);
		if (error != ASE_OK)
		{
			char message[124] = {};
			wrapper->getErrorMessage(message);
			std::fprintf(stderr, "AsioProbe: createBuffers failed with %ld: %s\n", error, message);
			return 2;
		}
		long inputLatency = 0, outputLatency = 0;
		wrapper->getLatencies(&inputLatency, &outputLatency);
		std::printf("latency input=%ld output=%ld frames\n", inputLatency, outputLatency);
		// The gate waits for this line before it measures; with stdout in a
		// file the CRT would otherwise hold it until exit.
		std::fflush(stdout);
		error = wrapper->start();
		if (error != ASE_OK)
		{
			char message[124] = {};
			wrapper->getErrorMessage(message);
			std::fprintf(stderr, "AsioProbe: start failed with %ld: %s\n", error, message);
			return 2;
		}
		Sleep(static_cast<DWORD>(a.seconds * 1000.0));
		wrapper->stop();
		StreamStats stats;
		if (staticWrapper != nullptr)
			stats = staticWrapper->stats();
		printProfile(staticWrapper);
		wrapper->disposeBuffers();

		std::printf("switches %lu (time-info %lu) reset-requests %lu\n", host.switches(), host.timeInfoSwitches(), host.resetRequests());
		if (staticWrapper != nullptr)
		{
			std::printf("blocks out=%llu in=%llu late out=%llu in=%llu gone out=%llu in=%llu stale=%u\n",
				static_cast<unsigned long long>(stats.blocks[0]), static_cast<unsigned long long>(stats.blocks[1]),
				static_cast<unsigned long long>(stats.late[0]), static_cast<unsigned long long>(stats.late[1]),
				static_cast<unsigned long long>(stats.gone[0]), static_cast<unsigned long long>(stats.gone[1]), stats.staleBlocks);
			std::printf("process-us max out=%u in=%u last out=%u in=%u\n",
				stats.maxProcessUs[0], stats.maxProcessUs[1], stats.lastProcessUs[0], stats.lastProcessUs[1]);
			if (static_cast<long>(stats.late[0] + stats.late[1]) > a.maxLate)
				return 4;
		}
		return host.switches() > 0 ? 0 : 2;
	}
}

int wmain(int argc, wchar_t** argv)
{
	Arguments a;
	if (!parse(argc, argv, a))
	{
		usage();
		return 1;
	}
	Logging::set(stderr, false, false, false);
	const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	Module targetModule, wrapperModule;
	IASIO* target = nullptr;
	IFakeAsioControl* control = nullptr;
	std::wstring targetClsid = L"{B7E3A9F4-52C1-4D0B-8A6E-1F9C3D5E7B21}";
	const bool realDriver = a.target.rfind(L"clsid:", 0) == 0;
	const bool wasapiTarget = a.target.rfind(L"wasapi:", 0) == 0;
	// A target with a device behind it: no fake control, no pump, timed run.
	const bool liveTarget = realDriver || wasapiTarget;
	eapo::asio::WasapiExclusiveTarget* wasapi = nullptr;
	if (a.target == L"fake")
	{
		target = new FakeAsioDriver();
	}
	else if (a.target.rfind(L"dll:", 0) == 0)
	{
		target = loadFromDll(a.target.substr(4), CLSID_FakeAsio, targetModule);
	}
	else if (realDriver)
	{
		targetClsid = a.target.substr(6);
		target = loadRealDriver(targetClsid);
	}
	else if (wasapiTarget)
	{
		// wasapi:{playback guid}[,{recording guid}]; either side may be
		// left empty. The Device: line sees the playback GUID (or the
		// recording one when there is no playback side).
		const std::wstring spec = a.target.substr(7);
		const size_t comma = spec.find(L',');
		const std::wstring renderGuid = comma == std::wstring::npos ? spec : spec.substr(0, comma);
		const std::wstring captureGuid = comma == std::wstring::npos ? std::wstring() : spec.substr(comma + 1);
		targetClsid = renderGuid.empty() ? captureGuid : renderGuid;
		wasapi = new eapo::asio::WasapiExclusiveTarget(renderGuid, captureGuid);
		target = wasapi;
	}
	else
	{
		usage();
		return 1;
	}
	if (target == nullptr)
		return 2;

	if (!liveTarget)
	{
		if (FAILED(target->QueryInterface(IID_IFakeAsioControl, reinterpret_cast<void**>(&control))))
		{
			std::fputs("AsioProbe: the target is not the fake driver\n", stderr);
			return 2;
		}
		FakeAsioConfig config;
		config.sampleRate = a.rate;
		config.preferredSize = a.frames;
		config.minSize = a.frames < 32 ? a.frames : 32;
		config.maxSize = a.frames > 1024 ? a.frames : 1024;
		config.inputChannels = a.inputs;
		config.outputChannels = a.outputs;
		config.sampleType = a.sampleType;
		config.seed = a.seed;
		control->configure(&config);
	}

	StreamOptions options;
	options.processInput = a.processInput;
	options.processOutput = a.processOutput;
	options.mode = a.mode == L"pipelined" ? Mode::Pipelined : Mode::Sync;
	options.configPath = a.config;
	options.deadlineUs = a.deadlineUs;
	options.daemonEndpoint = a.endpoint;
	options.daemonExePath = a.daemonExe;
	options.lingerMs = 2000;

	IASIO* wrapper = nullptr;
	AsioWrapper* staticWrapper = nullptr;
	if (a.wrapper == L"static")
	{
		std::unique_ptr<IStreamProcessor> processor;
		if (a.processor == L"inproc")
			processor = std::make_unique<eapo::asio::InProcProcessor>();
		else if (a.processor == L"passthrough")
			processor = std::make_unique<eapo::asio::PassthroughProcessor>();
		else if (a.processor == L"daemon-thread")
			processor = std::make_unique<eapo::asio::DaemonProcessor>(std::make_unique<eapo::asio::ThreadHostLink>(liveTarget));
		else if (a.processor == L"daemon")
			processor = std::make_unique<eapo::asio::DaemonProcessor>(std::make_unique<eapo::asio::Win32HostLink>());
		else
		{
			usage();
			return 1;
		}
		staticWrapper = new AsioWrapper(target, probeWrapperClsid, targetClsid, options, std::move(processor));
		wrapper = staticWrapper;
	}
	else if (a.wrapper.rfind(L"dll:", 0) == 0)
	{
		wrapper = wrapThroughDll(a.wrapper.substr(4), target, targetClsid, a, wrapperModule);
	}
	else
	{
		usage();
		return 1;
	}
	target->Release();      // the wrapper holds its own reference
	if (wrapper == nullptr)
		return 2;

	int result;
	if (liveTarget)
		result = runRealStream(a, wrapper, staticWrapper);
	else
		result = runFakeStream(a, wrapper, control, staticWrapper);
	if (wasapi != nullptr)
	{
		const eapo::asio::WasapiExclusiveTarget::Counters counters = wasapi->counters();
		std::printf("wasapi periods %llu input-underruns %llu output-misses %llu slow-events %llu event-interval avg %llu us max %llu us service max %llu us bridge %llu\n",
			static_cast<unsigned long long>(counters.periods), static_cast<unsigned long long>(counters.inputUnderruns),
			static_cast<unsigned long long>(counters.outputMisses), static_cast<unsigned long long>(counters.slowEvents),
			static_cast<unsigned long long>(counters.eventIntervalAvgUs), static_cast<unsigned long long>(counters.eventIntervalMaxUs),
			static_cast<unsigned long long>(counters.serviceMaxUs), static_cast<unsigned long long>(counters.bridge));
	}

	if (control != nullptr)
		control->Release();
	wrapper->Release();
	if (SUCCEEDED(comInit))
		CoUninitialize();
	std::printf("AsioProbe exit %d\n", result);
	return result;
}
