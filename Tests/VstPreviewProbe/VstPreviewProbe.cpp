/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The VST panel-preview probe: a disposable console harness behind the
	vst3-preview-probe workflow. It validates the two premises of the panel
	preview feed (Editor/helpers/PanelPreviewFeeder) on a real machine.

	  --plugin <path>    loads a VST3/VST2 plugin through the same
	                     VSTPluginInstance choreography the Editor preview
	                     uses (initialize, negotiate, prepareForProcessing,
	                     startProcessing, width-matched process calls), drives
	                     a sine through it and saves state back.
	                     --channels N negotiates an N-channel device instead
	                     of stereo, --stereo-input adds the upmixer layout
	                     (stereo input bus, N-channel output bus), and
	                     --input/--output <layout> negotiate an explicit
	                     VSTPlugin Input/Output contract (Stereo, 5.1, 7.1,
	                     7.1.4, ...), which fails the probe when refused.
	  --loopback         verifies that WASAPI loopback capture on the default
	                     render endpoint delivers float32 frames while this
	                     process renders a tone (on CI that endpoint is the
	                     Scream virtual driver).
	  --monitor <path>   drives the real PanelFeedEngine (the Qt-free core of
	                     the Editor's preview feed) against a plugin, with a
	                     sidecar loopback capture listening on the endpoint.
	                     With --expect-live the gate must open and the
	                     self-generated audio must be audible on the endpoint
	                     (fixture: TestVst3Plugin copied as ToneGenerator.vst3);
	                     with --expect-static a pass-through plugin fed silence
	                     must never open the gate.
	  --capture-only     a measurement sidecar for manual A/B runs: capture
	                     the endpoint for the given duration and assert
	                     --expect-audio or --expect-silence.
	  --panel            reproduces the Editor's VST3 panel-open sequence from a
	                     VSTPlugin config line. It varies the library-load thread,
	                     first instance, and the initialize/destroy thread and
	                     lifetime of repeated worker-owned instances before
	                     attaching a hidden HWND editor. A watchdog reports a stuck
	                     phase or any visible top-level window before terminating.
	  --list-vst-lines   parses a config and lists only the line number and library
	                     file name for each valid VSTPlugin command.

	The Qt timer face of the feed stays in the Editor; the engine underneath
	is compiled into this probe directly so the gate, the silence feed and
	the monitor playback run headless and end to end.
	Exit codes: 0 ok, 1 usage, 2 load/negotiation failure, 3 processing
	failure, 4 loopback/capture failure (or panel hang), 5 monitor failure
	(or a visible panel-probe window).
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <numbers>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Editor/helpers/PanelFeedEngine.h"
#include "engine/ConfigurationFileReader.h"
#include "filters/VSTPluginCommand.h"
#include "platform/windows/ComPtr.h"
#include "Tests/ProbeSupport/CommandLine.h"
#include "Tests/ProbeSupport/ToneMeter.h"
#include "Tests/ProbeSupport/WasapiEndpoint.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"

using winutil::ComApartment;
using winutil::ComPtr;
using winutil::CoTaskMem;

namespace
{
constexpr int blockFrames = 1024;
constexpr double sineFrequency = 440.0;
constexpr double sineAmplitude = 0.25;

bool isFloat32Format(const WAVEFORMATEX* format)
{
	if (format->wBitsPerSample != 32)
		return false;
	if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
		return true;
	if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		const WAVEFORMATEXTENSIBLE* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
		return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
	}
	return false;
}

// The calls into third-party plugin code, isolated behind SEH so a crash is
// reported as a probe verdict instead of a process abort. Kept free of
// objects requiring stack unwinding (MSVC C2712).
// Wall time of one host step, so a slow plugin load shows which call it
// spent its time in (LoadLibrary and InitDll, instance creation, setup).
class StepClock
{
public:
	StepClock() { QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start); }

	void lap(const wchar_t* step)
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		wprintf(L"TIME: %-28s %9.1f ms\n", step, (now.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart);
		start = now;
	}

private:
	LARGE_INTEGER frequency{};
	LARGE_INTEGER start{};
};

bool processFloatGuarded(VSTPluginInstance* instance, float** input, float** output, int frames) noexcept
{
	__try
	{
		instance->processBlock(input, output, static_cast<unsigned>(frames));
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

struct PluginProbeLayout
{
	int channels = 2;
	bool stereoInput = false;
	VST3BusLayout input = VST3BusLayout::Auto;
	VST3BusLayout output = VST3BusLayout::Auto;
};

const wchar_t* layoutLabel(const std::optional<VST3BusLayout>& layout)
{
	return layout ? vst3BusLayoutName(*layout) : L"unnamed";
}

int runPluginProbe(const std::wstring& pluginPath, double seconds, float sampleRate, bool expectNonsilent,
	const PluginProbeLayout& layout)
{
	StepClock clock;
	std::shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(pluginPath);
	const auto libraryResult = VSTPluginLibrary::load(library->getLibPath()).status;
	clock.lap(L"library initialize");
	if (!AbstractLibrary::succeeded(libraryResult))
	{
		wprintf(L"FAIL: library initialize returned %d for %s\n", static_cast<int>(libraryResult), pluginPath.c_str());
		return 2;
	}

	auto instanceOwner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
	VSTPluginInstance& instance = *instanceOwner;
	if (!instance.initialize())
	{
		wprintf(L"FAIL: plugin instance initialize failed\n");
		return 2;
	}
	clock.lap(L"instance initialize");
	wprintf(L"INFO: loaded '%s' (%hs)\n", instance.getName().c_str(), instance.isVST3() ? "VST3" : "VST2");

	VSTPluginInstance::NegotiationRequest request;
	request.channelCount = layout.channels;
	request.stereoInput = layout.stereoInput;
	if (layout.input != VST3BusLayout::Auto || layout.output != VST3BusLayout::Auto)
		request.contract = VST3BusContract{layout.input, layout.output};
	const auto negotiated = instance.negotiate(request);
	if (!negotiated.accepted)
	{
		wprintf(L"FAIL: plugin negotiation: %s\n", negotiated.reason.c_str());
		return 2;
	}
	clock.lap(L"negotiate channels");
	const int inputChannelCount = negotiated.inputs;
	const int outputChannelCount = negotiated.outputs;
	wprintf(L"INFO: negotiated %d in (%s) / %d out (%s)\n",
		inputChannelCount, layoutLabel(instance.getNegotiatedVST3InputLayout()),
		outputChannelCount, layoutLabel(instance.getNegotiatedVST3OutputLayout()));
	if (inputChannelCount <= 0 || outputChannelCount <= 0)
	{
		wprintf(L"FAIL: plugin reports no audio buses\n");
		return 2;
	}

	// The preview supplies floats; the instance selects the plug-in width.
	if (!instance.prepareForProcessing(sampleRate, blockFrames, VSTPluginInstance::InputWidth::Float))
	{
		wprintf(L"FAIL: prepare for processing\n");
		return 2;
	}
	clock.lap(L"prepare for processing");
	VSTPluginInstance::ProcessingLease processing;
	if (!processing.acquire(&instance))
	{
		wprintf(L"FAIL: start processing\n");
		return 2;
	}
	clock.lap(L"start processing");

	std::vector<std::vector<float>> inputFloat(inputChannelCount, std::vector<float>(blockFrames));
	std::vector<std::vector<float>> outputFloat(outputChannelCount, std::vector<float>(blockFrames));
	std::vector<float*> inputFloatPointers, outputFloatPointers;
	for (std::vector<float>& channel : inputFloat)
		inputFloatPointers.push_back(channel.data());
	for (std::vector<float>& channel : outputFloat)
		outputFloatPointers.push_back(channel.data());

	const long long totalFrames = static_cast<long long>(seconds * sampleRate);
	std::vector<double> channelPeaks(outputChannelCount, 0.0);
	double outputPeak = 0.0;
	bool outputFinite = true;
	long long position = 0;
	while (position < totalFrames)
	{
		probesupport::fillPlanarSine(inputFloatPointers.data(), static_cast<size_t>(inputChannelCount), blockFrames,
			static_cast<uint64_t>(position), sampleRate, sineFrequency, sineAmplitude);

		const bool survived = processFloatGuarded(&instance, inputFloatPointers.data(), outputFloatPointers.data(), blockFrames);
		if (!survived)
		{
			wprintf(L"FAIL: plugin crashed inside process\n");
			return 3;
		}

		for (int channel = 0; channel < outputChannelCount; channel++)
		{
			for (int i = 0; i < blockFrames; i++)
			{
				const double sample = static_cast<double>(outputFloat[channel][i]);
				if (!std::isfinite(sample))
					outputFinite = false;
				outputPeak = std::max(outputPeak, std::fabs(sample));
				channelPeaks[channel] = std::max(channelPeaks[channel], std::fabs(sample));
			}
		}
		position += blockFrames;
	}
	clock.lap(L"process");

	processing.reset();
	clock.lap(L"stop processing");

	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	instance.readFromEffect(chunkData, paramMap);
	clock.lap(L"read state");
	wprintf(L"INFO: processed %lld frames, output peak %.6f, state chunk %zu chars, %zu named params\n",
		position, outputPeak, chunkData.size(), paramMap.size());
	wprintf(L"INFO: output channel peaks");
	for (double peak : channelPeaks)
		wprintf(L" %.4f", peak);
	wprintf(L"\n");

	if (!outputFinite)
	{
		wprintf(L"FAIL: plugin produced non-finite output\n");
		return 3;
	}
	if (expectNonsilent && outputPeak <= 0.0)
	{
		wprintf(L"FAIL: output stayed silent but --expect-nonsilent was given\n");
		return 3;
	}
	wprintf(L"PASS: plugin probe\n");
	return 0;
}

int runLoopbackProbe(double seconds)
{
	ComPtr<IMMDeviceEnumerator> enumerator;
	HRESULT hr = probesupport::wasapi::createEnumerator(enumerator);
	if (FAILED(hr) || !enumerator)
	{
		wprintf(L"FAIL: device enumerator (hr=0x%08lx)\n", hr);
		return 4;
	}

	ComPtr<IMMDevice> device;
	hr = probesupport::wasapi::defaultEndpoint(enumerator.get(), eRender, device);
	if (FAILED(hr) || !device)
	{
		wprintf(L"FAIL: no default render endpoint (hr=0x%08lx)\n", hr);
		return 4;
	}

	ComPtr<IAudioClient> renderClient;
	hr = probesupport::wasapi::activateAudioClient(device.get(), renderClient);
	if (FAILED(hr) || !renderClient)
	{
		wprintf(L"FAIL: render IAudioClient (hr=0x%08lx)\n", hr);
		return 4;
	}

	CoTaskMem<WAVEFORMATEX> mixFormat;
	hr = renderClient->GetMixFormat(mixFormat.put());
	if (FAILED(hr) || !mixFormat)
	{
		wprintf(L"FAIL: GetMixFormat (hr=0x%08lx)\n", hr);
		return 4;
	}
	wprintf(L"INFO: mix format %lu Hz, %u channels, %u bits, tag %u\n",
		mixFormat->nSamplesPerSec, mixFormat->nChannels, mixFormat->wBitsPerSample, mixFormat->wFormatTag);

	// The feeder's core format assumption, checked against a live endpoint.
	if (!isFloat32Format(mixFormat.get()))
	{
		wprintf(L"FAIL: shared-mode mix format is not float32\n");
		return 4;
	}

	constexpr REFERENCE_TIME bufferDuration = 5000000;
	hr = renderClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0, mixFormat.get(), nullptr);
	if (FAILED(hr))
	{
		wprintf(L"FAIL: render Initialize (hr=0x%08lx)\n", hr);
		return 4;
	}
	ComPtr<IAudioRenderClient> renderService;
	hr = renderClient->GetService(__uuidof(IAudioRenderClient),
		reinterpret_cast<void**>(renderService.put()));
	if (FAILED(hr) || !renderService)
	{
		wprintf(L"FAIL: IAudioRenderClient (hr=0x%08lx)\n", hr);
		return 4;
	}
	UINT32 renderBufferFrames = 0;
	renderClient->GetBufferSize(&renderBufferFrames);

	ComPtr<IAudioClient> captureClient;
	hr = probesupport::wasapi::activateAudioClient(device.get(), captureClient);
	if (FAILED(hr) || !captureClient)
	{
		wprintf(L"FAIL: capture IAudioClient (hr=0x%08lx)\n", hr);
		return 4;
	}
	hr = captureClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
		bufferDuration, 0, mixFormat.get(), nullptr);
	if (FAILED(hr))
	{
		wprintf(L"FAIL: loopback Initialize (hr=0x%08lx)\n", hr);
		return 4;
	}
	ComPtr<IAudioCaptureClient> captureService;
	hr = captureClient->GetService(__uuidof(IAudioCaptureClient),
		reinterpret_cast<void**>(captureService.put()));
	if (FAILED(hr) || !captureService)
	{
		wprintf(L"FAIL: IAudioCaptureClient (hr=0x%08lx)\n", hr);
		return 4;
	}

	hr = captureClient->Start();
	if (FAILED(hr))
	{
		wprintf(L"FAIL: capture Start (hr=0x%08lx)\n", hr);
		return 4;
	}
	hr = renderClient->Start();
	if (FAILED(hr))
	{
		wprintf(L"FAIL: render Start (hr=0x%08lx)\n", hr);
		return 4;
	}

	const UINT32 channelCount = mixFormat->nChannels;
	const double sampleRate = mixFormat->nSamplesPerSec;
	long long renderedFrames = 0;
	long long capturedFrames = 0;
	double capturedPeak = 0.0;
	const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds * 1000.0);
	while (GetTickCount64() < deadline)
	{
		// Keep the render buffer topped up with the tone.
		UINT32 padding = 0;
		if (SUCCEEDED(renderClient->GetCurrentPadding(&padding)))
		{
			const UINT32 writable = renderBufferFrames - padding;
			if (writable > 0)
			{
				BYTE* renderData = nullptr;
				if (SUCCEEDED(renderService->GetBuffer(writable, &renderData)))
				{
					float* samples = reinterpret_cast<float*>(renderData);
					for (UINT32 i = 0; i < writable; i++)
					{
						// Amplitude-modulated at 1.5 Hz: a steady tone lets
						// level meters settle into a motionless reading, and
						// the panel-feed A/B keys on meter movement.
						const double t = (renderedFrames + i) / sampleRate;
						const double envelope = std::pow(std::sin(2.0 * std::numbers::pi_v<double> * 1.5 * t / 2.0), 2.0);
						const float value = static_cast<float>(probesupport::sineSample(
							sineFrequency, sineAmplitude * envelope, static_cast<uint64_t>(renderedFrames + i), sampleRate));
						for (UINT32 channel = 0; channel < channelCount; channel++)
							samples[i * channelCount + channel] = value;
					}
					renderService->ReleaseBuffer(writable, 0);
					renderedFrames += writable;
				}
			}
		}

		// Drain the loopback side, tracking what actually arrived.
		UINT32 packetFrames = 0;
		while (SUCCEEDED(captureService->GetNextPacketSize(&packetFrames)) && packetFrames > 0)
		{
			BYTE* data = nullptr;
			UINT32 framesAvailable = 0;
			DWORD flags = 0;
			if (FAILED(captureService->GetBuffer(&data, &framesAvailable, &flags, nullptr, nullptr)))
				break;
			if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0)
			{
				const float* samples = reinterpret_cast<const float*>(data);
				for (UINT32 i = 0; i < framesAvailable * channelCount; i++)
					capturedPeak = std::max(capturedPeak, static_cast<double>(std::fabs(samples[i])));
			}
			capturedFrames += framesAvailable;
			captureService->ReleaseBuffer(framesAvailable);
		}

		Sleep(10);
	}

	renderClient->Stop();
	captureClient->Stop();

	wprintf(L"INFO: rendered %lld frames, captured %lld frames, captured peak %.6f\n",
		renderedFrames, capturedFrames, capturedPeak);
	if (capturedFrames == 0)
	{
		wprintf(L"FAIL: loopback delivered no frames\n");
		return 4;
	}
	if (capturedPeak < 0.01)
	{
		wprintf(L"FAIL: loopback stayed silent while the tone was rendering\n");
		return 4;
	}
	wprintf(L"PASS: loopback probe\n");
	return 0;
}

// A loopback capture listening on the default render endpoint, used as the
// measuring ear of the monitor probe and of the manual A/B sidecar.
struct LoopbackTap
{
	ComPtr<IAudioClient> client;
	ComPtr<IAudioCaptureClient> service;
	long long capturedFrames = 0;
	double peak = 0.0;

	bool open()
	{
		ComPtr<IMMDeviceEnumerator> enumerator;
		HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
			CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
			reinterpret_cast<void**>(enumerator.put()));
		if (FAILED(hr) || !enumerator)
		{
			wprintf(L"FAIL: tap device enumerator (hr=0x%08lx)\n", hr);
			return false;
		}
		ComPtr<IMMDevice> device;
		hr = probesupport::wasapi::defaultEndpoint(enumerator.get(), eRender, device);
		if (FAILED(hr) || !device)
		{
			wprintf(L"FAIL: tap has no default render endpoint (hr=0x%08lx)\n", hr);
			return false;
		}
		hr = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
			reinterpret_cast<void**>(client.put()));
		if (FAILED(hr) || !client)
		{
			wprintf(L"FAIL: tap IAudioClient (hr=0x%08lx)\n", hr);
			return false;
		}
		CoTaskMem<WAVEFORMATEX> mixFormat;
		hr = client->GetMixFormat(mixFormat.put());
		if (FAILED(hr) || !mixFormat || !isFloat32Format(mixFormat.get()))
		{
			wprintf(L"FAIL: tap mix format unusable (hr=0x%08lx)\n", hr);
			return false;
		}
		channelCount = mixFormat->nChannels;
		constexpr REFERENCE_TIME bufferDuration = 5000000;
		hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
			bufferDuration, 0, mixFormat.get(), nullptr);
		if (FAILED(hr))
		{
			wprintf(L"FAIL: tap loopback Initialize (hr=0x%08lx)\n", hr);
			return false;
		}
		hr = client->GetService(__uuidof(IAudioCaptureClient),
			reinterpret_cast<void**>(service.put()));
		if (FAILED(hr) || !service)
		{
			wprintf(L"FAIL: tap IAudioCaptureClient (hr=0x%08lx)\n", hr);
			return false;
		}
		hr = client->Start();
		if (FAILED(hr))
		{
			wprintf(L"FAIL: tap Start (hr=0x%08lx)\n", hr);
			return false;
		}
		return true;
	}

	void drain()
	{
		UINT32 packetFrames = 0;
		while (SUCCEEDED(service->GetNextPacketSize(&packetFrames)) && packetFrames > 0)
		{
			BYTE* data = nullptr;
			UINT32 framesAvailable = 0;
			DWORD flags = 0;
			if (FAILED(service->GetBuffer(&data, &framesAvailable, &flags, nullptr, nullptr)))
				break;
			if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0)
			{
				const float* samples = reinterpret_cast<const float*>(data);
				for (UINT32 i = 0; i < framesAvailable * channelCount; i++)
					peak = std::max(peak, static_cast<double>(std::fabs(samples[i])));
			}
			capturedFrames += framesAvailable;
			service->ReleaseBuffer(framesAvailable);
		}
	}

private:
	UINT32 channelCount = 0;
};

// The audibility threshold the monitor assertions key on: comfortably above
// an idle endpoint's numeric noise, comfortably below the fixture tone.
constexpr double audiblePeak = 0.01;

int runMonitorProbe(const std::wstring& pluginPath, double seconds, bool expectLive)
{
	std::shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(pluginPath);
	const auto libraryResult = VSTPluginLibrary::load(library->getLibPath()).status;
	if (!AbstractLibrary::succeeded(libraryResult))
	{
		wprintf(L"FAIL: library initialize returned %d for %s\n", static_cast<int>(libraryResult), pluginPath.c_str());
		return 2;
	}
	auto instanceOwner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
	VSTPluginInstance& instance = *instanceOwner;
	if (!instance.initialize())
	{
		wprintf(L"FAIL: plugin instance initialize failed\n");
		return 2;
	}
	instance.negotiate({});
	wprintf(L"INFO: loaded '%s' (%hs), %d in / %d out\n", instance.getName().c_str(),
		instance.isVST3() ? "VST3" : "VST2", instance.numInputs(), instance.numOutputs());

	LoopbackTap tap;
	if (!tap.open())
		return 4;

	PanelFeedEngine engine;
	PanelFeedEngine::Options options;
	// The probe has no editor window; the engine owns the processing
	// lifecycle the way it does for VST2 effects.
	options.requireVst3EditorSession = false;
	options.monitorEnabled = true;
	if (!engine.start(&instance, options))
	{
		wprintf(L"FAIL: PanelFeedEngine did not start\n");
		return 5;
	}

	bool reachedRender = false;
	const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds * 1000.0);
	while (GetTickCount64() < deadline)
	{
		if (!engine.tick())
		{
			wprintf(L"FAIL: the feed engine shut itself down\n");
			return 5;
		}
		if (engine.gateState() == PanelMonitorGate::State::Render)
			reachedRender = true;
		tap.drain();
		Sleep(PanelFeedEngine::tickIntervalMs());
	}
	const long long renderedFrames = engine.renderedFrames();
	engine.stop();

	wprintf(L"INFO: gate %hs, rendered %lld frames, tap captured %lld frames, tap peak %.6f\n",
		reachedRender ? "opened" : "stayed in Listen", renderedFrames,
		tap.capturedFrames, tap.peak);

	if (expectLive)
	{
		if (!reachedRender || renderedFrames == 0)
		{
			wprintf(L"FAIL: the gate never opened for a self-generating plugin\n");
			return 5;
		}
		if (tap.peak < audiblePeak)
		{
			wprintf(L"FAIL: the monitor rendered but nothing was audible on the endpoint\n");
			return 5;
		}
		wprintf(L"PASS: monitor probe (live)\n");
	}
	else
	{
		// No tap assertion here: whether the endpoint is silent is a property
		// of the machine (a workstation hums, CI's Scream endpoint does not),
		// and either way it is not this probe's claim. The claim is that the
		// engine itself never rendered.
		if (reachedRender || renderedFrames != 0)
		{
			wprintf(L"FAIL: the gate opened for a pass-through plugin\n");
			return 5;
		}
		wprintf(L"PASS: monitor probe (static)\n");
	}
	return 0;
}

enum class PanelLoadThread
{
	Main,
	Worker
};

enum class FirstInstanceThread
{
	Main,
	Worker
};

enum class WorkerOperationThread
{
	Main,
	Worker
};

enum class WorkerDestroyTiming
{
	BeforeAttach,
	AfterPanel
};

struct PanelOptions
{
	std::wstring configPath;
	int lineNumber = 0;
	PanelLoadThread loadThread = PanelLoadThread::Worker;
	FirstInstanceThread firstInstanceThread = FirstInstanceThread::Worker;
	WorkerOperationThread workerInitializeThread = WorkerOperationThread::Worker;
	WorkerOperationThread workerDestroyThread = WorkerOperationThread::Worker;
	WorkerDestroyTiming workerDestroyTiming = WorkerDestroyTiming::AfterPanel;
	int workerCycles = 1;
	int timeoutSeconds = 20;
	int holdSeconds = 0;
	double pumpSeconds = 3.0;
};

struct PanelThreadIds
{
	DWORD main = 0;
	std::atomic<DWORD> loader{0};
	std::atomic<DWORD> worker{0};
};

long long elapsedMilliseconds(std::chrono::steady_clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

template<class Work>
auto timedPanelPhase(const wchar_t* name, Work&& work)
{
	const auto start = std::chrono::steady_clock::now();
	auto result = work();
	wprintf(L"PHASE %s %lld ms\n", name, elapsedMilliseconds(start));
	fflush(stdout);
	return result;
}

void pumpPendingMessages()
{
	MSG message;
	while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
	{
		if (message.message == WM_QUIT)
			continue;
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
}

void pumpUntilSignaled(HANDLE event)
{
	for (;;)
	{
		const DWORD result = MsgWaitForMultipleObjects(1, &event, FALSE, INFINITE, QS_ALLINPUT);
		if (result == WAIT_OBJECT_0)
			break;
		if (result == WAIT_OBJECT_0 + 1)
			pumpPendingMessages();
	}
	pumpPendingMessages();
}

std::wstring trimWhitespace(const std::wstring& value)
{
	const size_t first = value.find_first_not_of(L" \t");
	if (first == std::wstring::npos)
		return L"";
	const size_t last = value.find_last_not_of(L" \t");
	return value.substr(first, last - first + 1);
}

bool readConfigLines(const std::wstring& configPath, std::vector<std::wstring>& lines,
	std::wstring& reason)
{
	std::stringstream input = ConfigurationFileReader::readWithRetry(configPath);
	if (!input)
	{
		reason = L"could not read the config file";
		return false;
	}
	lines = ConfigurationFileReader::decodeLines(input);
	return true;
}

bool parseVstCommandLine(const std::wstring& line, VSTPluginCommand& command)
{
	const size_t separator = line.find(L':');
	if (separator == std::wstring::npos || trimWhitespace(line.substr(0, separator)) != L"VSTPlugin")
		return false;
	command = VSTPluginCommand::parse(line.substr(separator + 1));
	return true;
}

bool readPanelCommand(const std::wstring& configPath, int lineNumber, VSTPluginCommand& command,
	std::wstring& reason)
{
	std::vector<std::wstring> lines;
	if (!readConfigLines(configPath, lines, reason))
		return false;
	if (lineNumber <= 0 || static_cast<size_t>(lineNumber) > lines.size())
	{
		reason = L"line number is outside the config file";
		return false;
	}
	if (!parseVstCommandLine(lines[static_cast<size_t>(lineNumber - 1)], command))
	{
		reason = L"selected line is not a VSTPlugin command";
		return false;
	}
	if (!command.valid)
	{
		reason = L"VSTPlugin parse failed: " + command.error;
		return false;
	}
	if (command.libraryPath.empty())
	{
		reason = L"VSTPlugin line has no library path";
		return false;
	}
	return true;
}

int listVstLines(const std::wstring& configPath)
{
	std::vector<std::wstring> lines;
	std::wstring reason;
	if (!readConfigLines(configPath, lines, reason))
	{
		wprintf(L"FAIL: %s\n", reason.c_str());
		return 2;
	}
	for (size_t index = 0; index < lines.size(); index++)
	{
		VSTPluginCommand command;
		if (!parseVstCommandLine(lines[index], command) || !command.valid || command.libraryPath.empty())
			continue;
		const std::wstring fileName = std::filesystem::path(command.libraryPath).filename().wstring();
		wprintf(L"VST-LINE %zu %s\n", index + 1, fileName.c_str());
	}
	return 0;
}

VSTPluginInstance::NegotiationRequest panelNegotiationRequest(const VSTPluginCommand& command,
	const VSTPluginInstance& instance)
{
	VSTPluginInstance::NegotiationRequest request;
	request.stereoInput = command.stereoInput;
	if (command.hasBusContract)
	{
		request.contract = command.busContract;
		request.channelCount = std::max(vst3BusLayoutChannelCount(command.busContract.input),
			vst3BusLayoutChannelCount(command.busContract.output));
	}
	else
		request.channelCount = std::max({2, instance.numInputs(), instance.numOutputs()});
	return request;
}

bool preparePanelInstance(VSTPluginInstance& instance, const VSTPluginCommand& command,
	bool restoreState, std::wstring& reason)
{
	const auto negotiated = instance.negotiate(panelNegotiationRequest(command, instance));
	if (!negotiated.accepted)
	{
		reason = L"plug-in negotiation failed: " + negotiated.reason;
		return false;
	}
	if (restoreState)
	{
		const auto restored = instance.writeToEffect(command.chunkData, command.paramMap);
		if (!restored)
		{
			reason = L"plug-in state restore failed";
			if (restored.reason != nullptr)
				reason += L": " + std::wstring(restored.reason);
			return false;
		}
	}
	if (!instance.prepareForProcessing(48000.0f, 8192, VSTPluginInstance::InputWidth::Float))
	{
		reason = L"prepareForProcessing failed";
		return false;
	}
	return true;
}

struct JuceWindowRecord
{
	HWND window = nullptr;
	DWORD threadId = 0;
	std::wstring className;
};

struct JuceWindowEnumContext
{
	DWORD threadId = 0;
	std::vector<JuceWindowRecord>* records = nullptr;
};

BOOL CALLBACK collectJuceWindow(HWND window, LPARAM parameter)
{
	JuceWindowEnumContext* context = reinterpret_cast<JuceWindowEnumContext*>(parameter);
	DWORD processId = 0;
	const DWORD threadId = GetWindowThreadProcessId(window, &processId);
	if (processId != GetCurrentProcessId() || threadId != context->threadId)
		return TRUE;
	wchar_t title[64] = {};
	GetWindowTextW(window, title, static_cast<int>(std::size(title)));
	if (wcscmp(title, L"JUCEWindow") != 0)
		return TRUE;
	wchar_t className[256] = {};
	GetClassNameW(window, className, static_cast<int>(std::size(className)));
	context->records->push_back({window, threadId, className});
	return TRUE;
}

std::vector<JuceWindowRecord> inspectOwnJuceWindows()
{
	std::vector<JuceWindowRecord> records;
	JuceWindowEnumContext context{GetCurrentThreadId(), &records};
	EnumWindows(collectJuceWindow, reinterpret_cast<LPARAM>(&context));
	return records;
}

class PersistentLoader
{
public:
	PersistentLoader(std::wstring path, PanelThreadIds& threadIds)
		: ready(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
		release(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
		inspectRequest(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
		inspectDone(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
		worker([this, path = std::move(path), &threadIds]() { run(path, threadIds); })
	{
	}

	~PersistentLoader()
	{
		stop();
		CloseHandle(ready);
		CloseHandle(release);
		CloseHandle(inspectRequest);
		CloseHandle(inspectDone);
	}

	AbstractLibrary::InitializeResult wait()
	{
		pumpUntilSignaled(ready);
		return status;
	}

	std::vector<JuceWindowRecord> inspect()
	{
		SetEvent(inspectRequest);
		if (WaitForSingleObject(inspectDone, 2000) != WAIT_OBJECT_0)
			return {};
		std::lock_guard<std::mutex> lock(recordsMutex);
		return records;
	}

	void stop()
	{
		if (stopped)
			return;
		stopped = true;
		SetEvent(release);
		if (worker.joinable())
			worker.join();
	}

private:
	void run(const std::wstring& path, PanelThreadIds& threadIds)
	{
		threadIds.loader.store(GetCurrentThreadId(), std::memory_order_release);
		status = VSTPluginLibrary::load(path).status;
		SetEvent(ready);
		if (!AbstractLibrary::succeeded(status))
			return;
		HANDLE events[] = {release, inspectRequest};
		for (;;)
		{
			const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(std::size(events)), events, FALSE, INFINITE);
			if (wait == WAIT_OBJECT_0)
				return;
			if (wait == WAIT_OBJECT_0 + 1)
			{
				std::vector<JuceWindowRecord> found = inspectOwnJuceWindows();
				{
					std::lock_guard<std::mutex> lock(recordsMutex);
					records = std::move(found);
				}
				SetEvent(inspectDone);
			}
		}
	}

	HANDLE ready = nullptr;
	HANDLE release = nullptr;
	HANDLE inspectRequest = nullptr;
	HANDLE inspectDone = nullptr;
	AbstractLibrary::InitializeResult status = AbstractLibrary::InitializeResult::LoadingFailed;
	bool stopped = false;
	std::mutex recordsMutex;
	std::vector<JuceWindowRecord> records;
	std::thread worker;
};

class WorkerInstance
{
public:
	WorkerInstance(const std::shared_ptr<VSTPluginLibrary>& library, const VSTPluginCommand& command,
		PanelThreadIds& threadIds, WorkerOperationThread initializeThread,
		WorkerOperationThread destroyThread, WorkerDestroyTiming destroyTiming, int cycles)
		: ready(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
		release(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
		inspectRequest(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
		inspectDone(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
		initializeRequest(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
		initializeDone(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
		destroyRequest(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
		destroyDone(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
		finished(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
		worker([this, library, &command, &threadIds, initializeThread, destroyThread, destroyTiming, cycles]() {
			run(library, command, threadIds, initializeThread, destroyThread, destroyTiming, cycles);
		})
	{
	}

	~WorkerInstance()
	{
		stop();
		CloseHandle(ready);
		CloseHandle(release);
		CloseHandle(inspectRequest);
		CloseHandle(inspectDone);
		CloseHandle(initializeRequest);
		CloseHandle(initializeDone);
		CloseHandle(destroyRequest);
		CloseHandle(destroyDone);
		CloseHandle(finished);
	}

	bool wait(std::wstring& failure)
	{
		waitForWorkerEvent(ready);
		failure = reason;
		return succeeded;
	}

	std::vector<JuceWindowRecord> inspect()
	{
		SetEvent(inspectRequest);
		if (WaitForSingleObject(inspectDone, 2000) != WAIT_OBJECT_0)
			return {};
		std::lock_guard<std::mutex> lock(recordsMutex);
		return records;
	}

	void stop()
	{
		if (stopped)
			return;
		stopped = true;
		SetEvent(release);
		waitForWorkerEvent(finished);
		if (worker.joinable())
			worker.join();
	}

private:
	void waitForWorkerEvent(HANDLE finalEvent)
	{
		HANDLE events[] = {finalEvent, initializeRequest, destroyRequest};
		for (;;)
		{
			const DWORD wait = MsgWaitForMultipleObjects(static_cast<DWORD>(std::size(events)), events,
				FALSE, INFINITE, QS_ALLINPUT);
			if (wait == WAIT_OBJECT_0)
				break;
			if (wait == WAIT_OBJECT_0 + 1)
			{
				initializeResult = timedPanelPhase(L"worker-initialize", [&]() {
					return pendingInitialize != nullptr && pendingInitialize->initialize();
				});
				SetEvent(initializeDone);
			}
			else if (wait == WAIT_OBJECT_0 + 2)
			{
				timedPanelPhase(L"worker-destroy", [&]() {
					delete pendingDestroy;
					pendingDestroy = nullptr;
					return true;
				});
				SetEvent(destroyDone);
			}
			else if (wait == WAIT_OBJECT_0 + std::size(events))
				pumpPendingMessages();
		}
		pumpPendingMessages();
	}

	bool initializeInstance(VSTPluginInstance* instance, WorkerOperationThread initializeThread)
	{
		if (initializeThread == WorkerOperationThread::Worker)
		{
			return timedPanelPhase(L"worker-initialize", [&]() {
				return instance->initialize();
			});
		}
		pendingInitialize = instance;
		SetEvent(initializeRequest);
		WaitForSingleObject(initializeDone, INFINITE);
		pendingInitialize = nullptr;
		return initializeResult;
	}

	void destroyInstance(std::unique_ptr<VSTPluginInstance>& instance,
		WorkerOperationThread destroyThread)
	{
		if (!instance)
			return;
		if (destroyThread == WorkerOperationThread::Worker)
		{
			timedPanelPhase(L"worker-destroy", [&]() {
				instance.reset();
				return true;
			});
			return;
		}
		pendingDestroy = instance.release();
		SetEvent(destroyRequest);
		WaitForSingleObject(destroyDone, INFINITE);
	}

	void finishFailure()
	{
		SetEvent(ready);
		SetEvent(finished);
	}

	void run(const std::shared_ptr<VSTPluginLibrary>& library, const VSTPluginCommand& command,
		PanelThreadIds& threadIds, WorkerOperationThread initializeThread,
		WorkerOperationThread destroyThread, WorkerDestroyTiming destroyTiming, int cycles)
	{
		threadIds.worker.store(GetCurrentThreadId(), std::memory_order_release);
		std::unique_ptr<VSTPluginInstance> instance;
		std::unique_ptr<VSTPluginInstance> liveInstance;
		try
		{
			for (int cycle = 0; cycle < cycles; cycle++)
			{
				instance = timedPanelPhase(L"worker-create", [&]() {
					return VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
				});
				if (!initializeInstance(instance.get(), initializeThread))
				{
					reason = L"worker plug-in instance initialize failed";
					destroyInstance(instance, destroyThread);
					finishFailure();
					return;
				}
				if (!preparePanelInstance(*instance, command, false, reason))
				{
					reason = L"worker " + reason;
					destroyInstance(instance, destroyThread);
					finishFailure();
					return;
				}

				const bool keepUntilPanelEnds = destroyTiming == WorkerDestroyTiming::AfterPanel
					&& cycle == cycles - 1;
				if (keepUntilPanelEnds)
					liveInstance = std::move(instance);
				else
					destroyInstance(instance, destroyThread);
			}

			succeeded = true;
			SetEvent(ready);
			HANDLE events[] = {release, inspectRequest};
			for (;;)
			{
				const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(std::size(events)), events,
					FALSE, INFINITE);
				if (wait == WAIT_OBJECT_0)
					break;
				if (wait == WAIT_OBJECT_0 + 1)
				{
					std::vector<JuceWindowRecord> found = inspectOwnJuceWindows();
					{
						std::lock_guard<std::mutex> lock(recordsMutex);
						records = std::move(found);
					}
					SetEvent(inspectDone);
				}
			}
			destroyInstance(liveInstance, destroyThread);
			SetEvent(finished);
		}
		catch (...)
		{
			destroyInstance(instance, destroyThread);
			destroyInstance(liveInstance, destroyThread);
			reason = L"worker plug-in instance raised an exception";
			finishFailure();
		}
	}

	HANDLE ready = nullptr;
	HANDLE release = nullptr;
	HANDLE inspectRequest = nullptr;
	HANDLE inspectDone = nullptr;
	HANDLE initializeRequest = nullptr;
	HANDLE initializeDone = nullptr;
	HANDLE destroyRequest = nullptr;
	HANDLE destroyDone = nullptr;
	HANDLE finished = nullptr;
	VSTPluginInstance* pendingInitialize = nullptr;
	VSTPluginInstance* pendingDestroy = nullptr;
	bool initializeResult = false;
	bool succeeded = false;
	bool stopped = false;
	std::wstring reason;
	std::mutex recordsMutex;
	std::vector<JuceWindowRecord> records;
	std::thread worker;
};

void printJuceWindows(const PanelThreadIds& threadIds, const std::vector<JuceWindowRecord>& mainRecords,
	PersistentLoader* loader, WorkerInstance* instanceWorker)
{
	std::vector<JuceWindowRecord> records = mainRecords;
	if (loader != nullptr)
	{
		std::vector<JuceWindowRecord> loaderRecords = loader->inspect();
		records.insert(records.end(), loaderRecords.begin(), loaderRecords.end());
	}
	if (instanceWorker != nullptr)
	{
		std::vector<JuceWindowRecord> workerRecords = instanceWorker->inspect();
		records.insert(records.end(), workerRecords.begin(), workerRecords.end());
	}
	std::vector<HWND> printed;
	for (const JuceWindowRecord& record : records)
	{
		if (std::find(printed.begin(), printed.end(), record.window) != printed.end())
			continue;
		printed.push_back(record.window);
		wprintf(L"JUCE-WINDOW class=%s tid=%lu main=%lu loader=%lu worker=%lu\n",
			record.className.c_str(), record.threadId, threadIds.main,
			threadIds.loader.load(std::memory_order_acquire),
			threadIds.worker.load(std::memory_order_acquire));
	}
	fflush(stdout);
}

struct VisibleWindow
{
	wchar_t className[256] = {};
	wchar_t title[256] = {};
	bool found = false;
};

BOOL CALLBACK findVisibleProcessWindow(HWND window, LPARAM parameter)
{
	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	if (processId != GetCurrentProcessId() || IsWindowVisible(window) == FALSE)
		return TRUE;
	VisibleWindow* result = reinterpret_cast<VisibleWindow*>(parameter);
	GetClassNameW(window, result->className, static_cast<int>(std::size(result->className)));
	DWORD_PTR ignored = 0;
	SendMessageTimeoutW(window, WM_GETTEXT, static_cast<WPARAM>(std::size(result->title)),
		reinterpret_cast<LPARAM>(result->title), SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &ignored);
	result->found = true;
	return FALSE;
}

class PanelWatchdog
{
public:
	PanelWatchdog(int timeoutSeconds, int holdSeconds, std::function<void()> reportJuceWindows)
		: timeoutMilliseconds(static_cast<ULONGLONG>(timeoutSeconds) * 1000),
		holdMilliseconds(static_cast<ULONGLONG>(holdSeconds) * 1000),
		reportJuceWindows(std::move(reportJuceWindows)), worker([this]() { run(); })
	{
	}

	~PanelWatchdog()
	{
		finish();
	}

	void arm(const wchar_t* name)
	{
		phaseStarted.store(GetTickCount64(), std::memory_order_relaxed);
		phase.store(name, std::memory_order_release);
		armed.store(true, std::memory_order_release);
	}

	void next(const wchar_t* name)
	{
		phaseStarted.store(GetTickCount64(), std::memory_order_relaxed);
		phase.store(name, std::memory_order_release);
	}

	void finish()
	{
		stopped.store(true, std::memory_order_release);
		if (worker.joinable())
			worker.join();
	}

private:
	bool stopForVisibleWindow()
	{
		VisibleWindow visible;
		EnumWindows(findVisibleProcessWindow, reinterpret_cast<LPARAM>(&visible));
		if (!visible.found)
			return false;
		wprintf(L"VISIBLE-WINDOW class=%s title=%s\n", visible.className, visible.title);
		fflush(stdout);
		TerminateProcess(GetCurrentProcess(), 5);
		return true;
	}

	void run()
	{
		while (!stopped.load(std::memory_order_acquire))
		{
			if (stopForVisibleWindow())
				return;
			if (armed.load(std::memory_order_acquire))
			{
				const ULONGLONG started = phaseStarted.load(std::memory_order_relaxed);
				const ULONGLONG elapsed = GetTickCount64() - started;
				if (elapsed >= timeoutMilliseconds)
				{
					const wchar_t* name = phase.load(std::memory_order_acquire);
					wprintf(L"PANEL-HANG phase=%s after %llu ms pid=%lu\n", name, elapsed, GetCurrentProcessId());
					fflush(stdout);
					reportJuceWindows();
					const ULONGLONG holdEnd = GetTickCount64() + holdMilliseconds;
					while (GetTickCount64() < holdEnd)
					{
						if (stopForVisibleWindow())
							return;
						Sleep(250);
					}
					TerminateProcess(GetCurrentProcess(), 4);
					return;
				}
			}
			Sleep(250);
		}
	}

	const ULONGLONG timeoutMilliseconds;
	const ULONGLONG holdMilliseconds;
	std::function<void()> reportJuceWindows;
	std::atomic<bool> stopped{false};
	std::atomic<bool> armed{false};
	std::atomic<ULONGLONG> phaseStarted{0};
	std::atomic<const wchar_t*> phase{L"setup"};
	std::thread worker;
};

LRESULT CALLBACK panelParentWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProcW(window, message, wParam, lParam);
}

HWND createHiddenPanelParent()
{
	const wchar_t* className = L"EqualizerAPOVstPreviewProbeParent";
	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.lpfnWndProc = panelParentWindowProc;
	windowClass.hInstance = GetModuleHandleW(nullptr);
	windowClass.lpszClassName = className;
	if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return nullptr;
	return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className, L"", WS_POPUP,
		-32000, -32000, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

int runPanelProbe(const PanelOptions& options)
{
	ComApartment apartment(COINIT_APARTMENTTHREADED);
	if (!apartment.isUsable())
	{
		wprintf(L"FAIL: panel COM initialization (hr=0x%08lx)\n", apartment.status());
		return 2;
	}

	VSTPluginCommand command;
	std::wstring reason;
	if (!readPanelCommand(options.configPath, options.lineNumber, command, reason))
	{
		wprintf(L"FAIL: %s\n", reason.c_str());
		return 2;
	}

	PanelThreadIds threadIds;
	threadIds.main = GetCurrentThreadId();
	std::unique_ptr<PersistentLoader> loader;
	std::unique_ptr<WorkerInstance> instanceWorker;
	std::vector<JuceWindowRecord> mainJuceWindows;
	PanelWatchdog watchdog(options.timeoutSeconds, options.holdSeconds, [&]() {
		printJuceWindows(threadIds, mainJuceWindows, loader.get(), instanceWorker.get());
	});

	std::shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(command.libraryPath);
	const auto loadResult = timedPanelPhase(L"load", [&]() {
		if (options.loadThread == PanelLoadThread::Worker)
		{
			loader = std::make_unique<PersistentLoader>(command.libraryPath, threadIds);
			return loader->wait();
		}
		threadIds.loader.store(threadIds.main, std::memory_order_release);
		return VSTPluginLibrary::load(command.libraryPath).status;
	});
	if (!AbstractLibrary::succeeded(loadResult))
	{
		wprintf(L"FAIL: library load returned %d\n", static_cast<int>(loadResult));
		return 2;
	}

	std::unique_ptr<VSTPluginInstance> instance;
	const auto setUpMainInstance = [&]() {
		instance = timedPanelPhase(L"create", [&]() {
			return VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
		});
		const bool initialized = timedPanelPhase(L"initialize", [&]() {
			return instance->initialize();
		});
		if (!initialized)
		{
			reason = L"main plug-in instance initialize failed";
			return false;
		}
		const auto negotiated = timedPanelPhase(L"negotiate", [&]() {
			return instance->negotiate(panelNegotiationRequest(command, *instance));
		});
		if (!negotiated.accepted)
		{
			reason = L"plug-in negotiation failed: " + negotiated.reason;
			return false;
		}
		const auto restored = timedPanelPhase(L"state", [&]() {
			return instance->writeToEffect(command.chunkData, command.paramMap);
		});
		if (!restored)
		{
			reason = L"plug-in state restore failed";
			if (restored.reason != nullptr)
				reason += L": " + std::wstring(restored.reason);
			return false;
		}
		const bool prepared = timedPanelPhase(L"prepare", [&]() {
			return instance->prepareForProcessing(48000.0f, 8192, VSTPluginInstance::InputWidth::Float);
		});
		if (!prepared)
		{
			reason = L"prepareForProcessing failed";
			return false;
		}
		return true;
	};
	const auto setUpWorkerInstance = [&]() {
		instanceWorker = std::make_unique<WorkerInstance>(library, command, threadIds,
			options.workerInitializeThread, options.workerDestroyThread,
			options.workerDestroyTiming, options.workerCycles);
		return instanceWorker->wait(reason);
	};

	if (options.firstInstanceThread == FirstInstanceThread::Worker)
	{
		if (!setUpWorkerInstance() || !setUpMainInstance())
		{
			wprintf(L"FAIL: %s\n", reason.c_str());
			return 2;
		}
	}
	else
	{
		if (!setUpMainInstance() || !setUpWorkerInstance())
		{
			wprintf(L"FAIL: %s\n", reason.c_str());
			return 2;
		}
	}

	HWND parent = timedPanelPhase(L"window", []() { return createHiddenPanelParent(); });
	if (parent == nullptr)
	{
		wprintf(L"FAIL: hidden parent window creation failed (gle=%lu)\n", GetLastError());
		return 2;
	}
	mainJuceWindows = inspectOwnJuceWindows();

	short width = 0;
	short height = 0;
	watchdog.arm(L"attach");
	const auto attachStarted = std::chrono::steady_clock::now();
	const bool attached = instance->startEditing(parent, &width, &height, 1.0);
	wprintf(L"PHASE attach %lld ms\n", elapsedMilliseconds(attachStarted));
	fflush(stdout);
	if (!attached)
	{
		mainJuceWindows = inspectOwnJuceWindows();
		printJuceWindows(threadIds, mainJuceWindows, loader.get(), instanceWorker.get());
		watchdog.finish();
		DestroyWindow(parent);
		wprintf(L"FAIL: startEditing returned false\n");
		return 2;
	}

	watchdog.next(L"pump");
	const auto pumpStarted = std::chrono::steady_clock::now();
	const ULONGLONG pumpEnd = GetTickCount64() + static_cast<ULONGLONG>(options.pumpSeconds * 1000.0);
	while (GetTickCount64() < pumpEnd)
	{
		pumpPendingMessages();
		const ULONGLONG remaining = pumpEnd - GetTickCount64();
		MsgWaitForMultipleObjects(0, nullptr, FALSE,
			static_cast<DWORD>(std::min<ULONGLONG>(remaining, 50)), QS_ALLINPUT);
	}
	pumpPendingMessages();
	wprintf(L"PHASE pump %lld ms\n", elapsedMilliseconds(pumpStarted));
	fflush(stdout);

	watchdog.next(L"detach");
	const auto detachStarted = std::chrono::steady_clock::now();
	instance->stopEditing();
	wprintf(L"PHASE detach %lld ms\n", elapsedMilliseconds(detachStarted));
	fflush(stdout);
	DestroyWindow(parent);

	mainJuceWindows = inspectOwnJuceWindows();
	printJuceWindows(threadIds, mainJuceWindows, loader.get(), instanceWorker.get());
	watchdog.next(L"destroy");
	const auto destroyStarted = std::chrono::steady_clock::now();
	instance.reset();
	wprintf(L"PHASE destroy %lld ms\n", elapsedMilliseconds(destroyStarted));
	fflush(stdout);
	watchdog.finish();

	instanceWorker.reset();
	loader.reset();
	wprintf(L"PANEL-OK\n");
	fflush(stdout);
	return 0;
}

int runCaptureProbe(double seconds, bool expectAudio)
{
	LoopbackTap tap;
	if (!tap.open())
		return 4;
	const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds * 1000.0);
	while (GetTickCount64() < deadline)
	{
		tap.drain();
		Sleep(10);
	}
	wprintf(L"INFO: captured %lld frames, peak %.6f\n", tap.capturedFrames, tap.peak);
	if (expectAudio && tap.peak < audiblePeak)
	{
		wprintf(L"FAIL: the endpoint stayed silent but --expect-audio was given\n");
		return 4;
	}
	if (!expectAudio && tap.peak >= audiblePeak)
	{
		wprintf(L"FAIL: the endpoint carried audio but --expect-silence was given\n");
		return 4;
	}
	wprintf(L"PASS: capture probe (%hs)\n", expectAudio ? "audio" : "silence");
	return 0;
}
}

int wmain(int argc, wchar_t* argv[])
{
	std::wstring pluginPath;
	std::wstring monitorPluginPath;
	double seconds = 2.0;
	float sampleRate = 48000.0f;
	bool expectNonsilent = false;
	bool loopback = false;
	bool expectLive = true;
	bool captureOnly = false;
	bool expectAudio = true;
	bool panel = false;
	bool listVst = false;
	PanelOptions panelOptions;

	const wchar_t* usage =
		L"Usage: VstPreviewProbe [--plugin <path.vst3|path.dll>] [--seconds N] [--rate N]\n"
		L"                       [--channels N] [--stereo-input] [--input <layout>] [--output <layout>]\n"
		L"                       [--expect-nonsilent] [--loopback]\n"
		L"                       [--monitor <path.vst3|path.dll>] [--expect-live | --expect-static]\n"
		L"                       [--capture-only] [--expect-audio | --expect-silence]\n"
		L"       VstPreviewProbe --list-vst-lines --config <config.txt>\n"
		L"       VstPreviewProbe --panel --config <config.txt> --line <N>\n"
		L"                       [--load-on main|worker] [--first-instance main|worker]\n"
		L"                       [--worker-init-on main|worker]\n"
		L"                       [--worker-destroy-on main|worker]\n"
		L"                       [--worker-destroy before-attach|after-panel]\n"
		L"                       [--worker-cycles N]\n"
		L"                       [--timeout S] [--hold S] [--seconds S]\n";

	probesupport::CommandLine commandLine;
	TCLAP::CmdLine& parser = commandLine.parser();
	probesupport::SetBoolVisitor expectLiveVisitor(expectLive, true);
	probesupport::SetBoolVisitor expectStaticVisitor(expectLive, false);
	probesupport::SetBoolVisitor expectAudioVisitor(expectAudio, true);
	probesupport::SetBoolVisitor expectSilenceVisitor(expectAudio, false);
	TCLAP::MultiArg<std::string> pluginArg("", "plugin", "", false, "value", parser);
	TCLAP::MultiArg<std::string> monitorArg("", "monitor", "", false, "value", parser);
	TCLAP::MultiArg<std::string> secondsArg("", "seconds", "", false, "value", parser);
	TCLAP::MultiArg<std::string> rateArg("", "rate", "", false, "value", parser);
	TCLAP::MultiArg<std::string> channelsArg("", "channels", "", false, "value", parser);
	TCLAP::MultiArg<std::string> inputLayoutArg("", "input", "", false, "value", parser);
	TCLAP::MultiArg<std::string> outputLayoutArg("", "output", "", false, "value", parser);
	TCLAP::MultiArg<std::string> configArg("", "config", "", false, "value", parser);
	TCLAP::MultiArg<std::string> lineArg("", "line", "", false, "value", parser);
	TCLAP::MultiArg<std::string> loadOnArg("", "load-on", "", false, "value", parser);
	TCLAP::MultiArg<std::string> firstInstanceArg("", "first-instance", "", false, "value", parser);
	TCLAP::MultiArg<std::string> workerInitializeArg("", "worker-init-on", "", false, "value", parser);
	TCLAP::MultiArg<std::string> workerDestroyOnArg("", "worker-destroy-on", "", false, "value", parser);
	TCLAP::MultiArg<std::string> workerDestroyArg("", "worker-destroy", "", false, "value", parser);
	TCLAP::MultiArg<std::string> workerCyclesArg("", "worker-cycles", "", false, "value", parser);
	TCLAP::MultiArg<std::string> timeoutArg("", "timeout", "", false, "value", parser);
	TCLAP::MultiArg<std::string> holdArg("", "hold", "", false, "value", parser);
	TCLAP::MultiSwitchArg stereoInputArg("", "stereo-input", "", parser);
	TCLAP::MultiSwitchArg expectNonsilentArg("", "expect-nonsilent", "", parser);
	TCLAP::MultiSwitchArg expectLiveArg("", "expect-live", "", parser, 0, &expectLiveVisitor);
	TCLAP::MultiSwitchArg expectStaticArg("", "expect-static", "", parser, 0, &expectStaticVisitor);
	TCLAP::MultiSwitchArg expectAudioArg("", "expect-audio", "", parser, 0, &expectAudioVisitor);
	TCLAP::MultiSwitchArg expectSilenceArg("", "expect-silence", "", parser, 0, &expectSilenceVisitor);
	TCLAP::MultiSwitchArg loopbackArg("", "loopback", "", parser);
	TCLAP::MultiSwitchArg captureOnlyArg("", "capture-only", "", parser);
	TCLAP::MultiSwitchArg panelArg("", "panel", "", parser);
	TCLAP::MultiSwitchArg listVstArg("", "list-vst-lines", "", parser);
	if (!commandLine.parse(argc, argv))
	{
		wprintf(L"%s", usage);
		return 1;
	}

	if (pluginArg.isSet())
		pluginPath = probesupport::lastWide(pluginArg);
	if (monitorArg.isSet())
		monitorPluginPath = probesupport::lastWide(monitorArg);
	if (secondsArg.isSet())
		seconds = _wtof(probesupport::lastWide(secondsArg).c_str());
	if (rateArg.isSet())
		sampleRate = static_cast<float>(_wtof(probesupport::lastWide(rateArg).c_str()));
	expectNonsilent = expectNonsilentArg.getValue() != 0;
	PluginProbeLayout pluginLayout;
	if (channelsArg.isSet())
		pluginLayout.channels = std::max(1, _wtoi(probesupport::lastWide(channelsArg).c_str()));
	pluginLayout.stereoInput = stereoInputArg.getValue() != 0;
	if ((inputLayoutArg.isSet() && !parseVST3BusLayout(probesupport::lastWide(inputLayoutArg), pluginLayout.input))
		|| (outputLayoutArg.isSet() && !parseVST3BusLayout(probesupport::lastWide(outputLayoutArg), pluginLayout.output)))
	{
		wprintf(L"%s", usage);
		return 1;
	}
	loopback = loopbackArg.getValue() != 0;
	captureOnly = captureOnlyArg.getValue() != 0;
	panel = panelArg.getValue() != 0;
	listVst = listVstArg.getValue() != 0;
	if (listVst)
	{
		if (!configArg.isSet() || panel || !pluginPath.empty() || !monitorPluginPath.empty()
			|| loopback || captureOnly)
		{
			wprintf(L"%s", usage);
			return 1;
		}
		return listVstLines(probesupport::lastWide(configArg));
	}
	if (panel)
	{
		if (!configArg.isSet() || !lineArg.isSet() || !pluginPath.empty() || !monitorPluginPath.empty()
			|| loopback || captureOnly)
		{
			wprintf(L"%s", usage);
			return 1;
		}
		panelOptions.configPath = probesupport::lastWide(configArg);
		panelOptions.lineNumber = _wtoi(probesupport::lastWide(lineArg).c_str());
		if (secondsArg.isSet())
			panelOptions.pumpSeconds = seconds;
		if (timeoutArg.isSet())
			panelOptions.timeoutSeconds = _wtoi(probesupport::lastWide(timeoutArg).c_str());
		if (holdArg.isSet())
			panelOptions.holdSeconds = _wtoi(probesupport::lastWide(holdArg).c_str());
		if (workerCyclesArg.isSet())
			panelOptions.workerCycles = _wtoi(probesupport::lastWide(workerCyclesArg).c_str());

		const std::wstring loadOn = loadOnArg.isSet() ? probesupport::lastWide(loadOnArg) : L"worker";
		const std::wstring firstInstance = firstInstanceArg.isSet()
			? probesupport::lastWide(firstInstanceArg) : L"worker";
		const std::wstring workerInitialize = workerInitializeArg.isSet()
			? probesupport::lastWide(workerInitializeArg) : L"worker";
		const std::wstring workerDestroyOn = workerDestroyOnArg.isSet()
			? probesupport::lastWide(workerDestroyOnArg) : L"worker";
		const std::wstring workerDestroy = workerDestroyArg.isSet()
			? probesupport::lastWide(workerDestroyArg) : L"after-panel";
		if (loadOn == L"main")
			panelOptions.loadThread = PanelLoadThread::Main;
		else if (loadOn != L"worker")
		{
			wprintf(L"%s", usage);
			return 1;
		}
		if (firstInstance == L"main")
			panelOptions.firstInstanceThread = FirstInstanceThread::Main;
		else if (firstInstance != L"worker")
		{
			wprintf(L"%s", usage);
			return 1;
		}
		if (workerInitialize == L"main")
			panelOptions.workerInitializeThread = WorkerOperationThread::Main;
		else if (workerInitialize != L"worker")
		{
			wprintf(L"%s", usage);
			return 1;
		}
		if (workerDestroyOn == L"main")
			panelOptions.workerDestroyThread = WorkerOperationThread::Main;
		else if (workerDestroyOn != L"worker")
		{
			wprintf(L"%s", usage);
			return 1;
		}
		if (workerDestroy == L"before-attach")
			panelOptions.workerDestroyTiming = WorkerDestroyTiming::BeforeAttach;
		else if (workerDestroy != L"after-panel")
		{
			wprintf(L"%s", usage);
			return 1;
		}
		if (panelOptions.configPath.empty() || panelOptions.lineNumber <= 0
			|| panelOptions.workerCycles <= 0 || panelOptions.timeoutSeconds <= 0
			|| panelOptions.holdSeconds < 0
			|| !std::isfinite(panelOptions.pumpSeconds) || panelOptions.pumpSeconds < 0.0)
		{
			wprintf(L"%s", usage);
			return 1;
		}
		return runPanelProbe(panelOptions);
	}
	if (pluginPath.empty() && monitorPluginPath.empty() && !loopback && !captureOnly)
	{
		wprintf(L"%s", usage);
		return 1;
	}

	ComApartment apartment(COINIT_MULTITHREADED);
	if (!apartment.isUsable())
	{
		wprintf(L"FAIL: COM initialization (hr=0x%08lx)\n", apartment.status());
		return 4;
	}

	if (!pluginPath.empty())
	{
		const int result = runPluginProbe(pluginPath, seconds, sampleRate, expectNonsilent, pluginLayout);
		if (result != 0)
			return result;
	}
	if (loopback)
	{
		const int result = runLoopbackProbe(seconds);
		if (result != 0)
			return result;
	}
	if (!monitorPluginPath.empty())
	{
		const int result = runMonitorProbe(monitorPluginPath, seconds, expectLive);
		if (result != 0)
			return result;
	}
	if (captureOnly)
	{
		const int result = runCaptureProbe(seconds, expectAudio);
		if (result != 0)
			return result;
	}
	return 0;
}
