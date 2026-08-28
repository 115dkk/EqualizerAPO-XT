/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The VST panel-preview probe: a disposable console harness behind the
	vst3-preview-probe workflow. It validates the two premises of the panel
	preview feed (Editor/helpers/PanelPreviewFeeder) on a real machine.

	  --plugin <path>    loads a VST3/VST2 plugin through the same
	                     VSTPluginInstance choreography the Editor preview
	                     uses (initialize, negotiate, prepareForProcessing,
	                     startProcessing, width-matched process calls), drives
	                     a sine through it and saves state back.
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

	The Qt timer face of the feed stays in the Editor; the engine underneath
	is compiled into this probe directly so the gate, the silence feed and
	the monitor playback run headless and end to end.
	Exit codes: 0 ok, 1 usage, 2 load/negotiation failure, 3 processing
	failure, 4 loopback/capture failure, 5 monitor failure.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>

#include "Editor/helpers/PanelFeedEngine.h"
#include "platform/windows/ComPtr.h"
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
bool processFloatGuarded(VSTPluginInstance* instance, float** input, float** output, int frames) noexcept
{
	__try
	{
		instance->processReplacing(input, output, frames);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

bool processDoubleGuarded(VSTPluginInstance* instance, double** input, double** output, int frames) noexcept
{
	__try
	{
		instance->processDoubleReplacing(input, output, frames);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

int runPluginProbe(const std::wstring& pluginPath, double seconds, float sampleRate, bool expectNonsilent)
{
	std::shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(pluginPath);
	const int libraryResult = library->initialize();
	if (libraryResult < 0)
	{
		wprintf(L"FAIL: library initialize returned %d for %s\n", libraryResult, pluginPath.c_str());
		return 2;
	}

	VSTPluginInstance instance(library, 1);
	if (!instance.initialize())
	{
		wprintf(L"FAIL: plugin instance initialize failed\n");
		return 2;
	}
	wprintf(L"INFO: loaded '%s' (%hs)\n", instance.getName().c_str(), instance.isVST3() ? "VST3" : "VST2");

	instance.negotiateChannelCount(2);
	const int inputChannelCount = instance.numInputs();
	const int outputChannelCount = instance.numOutputs();
	wprintf(L"INFO: negotiated %d in / %d out\n", inputChannelCount, outputChannelCount);
	if (inputChannelCount <= 0 || outputChannelCount <= 0)
	{
		wprintf(L"FAIL: plugin reports no audio buses\n");
		return 2;
	}

	// The same width rule as the Editor's preview feed: a double-capable
	// processor was set up with kSample64 by prepareForProcessing and must
	// be fed doubles.
	const bool useDouble = instance.canDoubleReplacing();
	wprintf(L"INFO: processing width %hs\n", useDouble ? "double64" : "float32");

	instance.prepareForProcessing(sampleRate, blockFrames);
	instance.startProcessing();

	std::vector<std::vector<double>> inputDouble(inputChannelCount, std::vector<double>(blockFrames));
	std::vector<std::vector<double>> outputDouble(outputChannelCount, std::vector<double>(blockFrames));
	std::vector<std::vector<float>> inputFloat(inputChannelCount, std::vector<float>(blockFrames));
	std::vector<std::vector<float>> outputFloat(outputChannelCount, std::vector<float>(blockFrames));
	std::vector<double*> inputDoublePointers, outputDoublePointers;
	std::vector<float*> inputFloatPointers, outputFloatPointers;
	for (std::vector<double>& channel : inputDouble)
		inputDoublePointers.push_back(channel.data());
	for (std::vector<double>& channel : outputDouble)
		outputDoublePointers.push_back(channel.data());
	for (std::vector<float>& channel : inputFloat)
		inputFloatPointers.push_back(channel.data());
	for (std::vector<float>& channel : outputFloat)
		outputFloatPointers.push_back(channel.data());

	const long long totalFrames = static_cast<long long>(seconds * sampleRate);
	double outputPeak = 0.0;
	bool outputFinite = true;
	long long position = 0;
	while (position < totalFrames)
	{
		for (int channel = 0; channel < inputChannelCount; channel++)
		{
			for (int i = 0; i < blockFrames; i++)
			{
				const double sample = sineAmplitude
					* std::sin(2.0 * 3.14159265358979323846 * sineFrequency * (position + i) / sampleRate);
				if (useDouble)
					inputDouble[channel][i] = sample;
				else
					inputFloat[channel][i] = static_cast<float>(sample);
			}
		}

		const bool survived = useDouble
			? processDoubleGuarded(&instance, inputDoublePointers.data(), outputDoublePointers.data(), blockFrames)
			: processFloatGuarded(&instance, inputFloatPointers.data(), outputFloatPointers.data(), blockFrames);
		if (!survived)
		{
			wprintf(L"FAIL: plugin crashed inside process\n");
			return 3;
		}

		for (int channel = 0; channel < outputChannelCount; channel++)
		{
			for (int i = 0; i < blockFrames; i++)
			{
				const double sample = useDouble ? outputDouble[channel][i]
					: static_cast<double>(outputFloat[channel][i]);
				if (!std::isfinite(sample))
					outputFinite = false;
				outputPeak = std::max(outputPeak, std::fabs(sample));
			}
		}
		position += blockFrames;
	}

	instance.stopProcessing();

	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	instance.readFromEffect(chunkData, paramMap);
	wprintf(L"INFO: processed %lld frames, output peak %.6f, state chunk %zu chars, %zu named params\n",
		position, outputPeak, chunkData.size(), paramMap.size());

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
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
		CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
		reinterpret_cast<void**>(enumerator.put()));
	if (FAILED(hr) || !enumerator)
	{
		wprintf(L"FAIL: device enumerator (hr=0x%08lx)\n", hr);
		return 4;
	}

	ComPtr<IMMDevice> device;
	hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put());
	if (FAILED(hr) || !device)
	{
		wprintf(L"FAIL: no default render endpoint (hr=0x%08lx)\n", hr);
		return 4;
	}

	ComPtr<IAudioClient> renderClient;
	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
		reinterpret_cast<void**>(renderClient.put()));
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
	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
		reinterpret_cast<void**>(captureClient.put()));
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
						const double envelope = std::pow(std::sin(2.0 * 3.14159265358979323846 * 1.5 * t / 2.0), 2.0);
						const float value = static_cast<float>(sineAmplitude * envelope
							* std::sin(2.0 * 3.14159265358979323846 * sineFrequency * t));
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
		hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put());
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
	const int libraryResult = library->initialize();
	if (libraryResult < 0)
	{
		wprintf(L"FAIL: library initialize returned %d for %s\n", libraryResult, pluginPath.c_str());
		return 2;
	}
	VSTPluginInstance instance(library, 1);
	if (!instance.initialize())
	{
		wprintf(L"FAIL: plugin instance initialize failed\n");
		return 2;
	}
	instance.negotiateChannelCount(2);
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

	const wchar_t* usage =
		L"Usage: VstPreviewProbe [--plugin <path.vst3|path.dll>] [--seconds N] [--rate N]\n"
		L"                       [--expect-nonsilent] [--loopback]\n"
		L"                       [--monitor <path.vst3|path.dll>] [--expect-live | --expect-static]\n"
		L"                       [--capture-only] [--expect-audio | --expect-silence]\n";

	for (int i = 1; i < argc; i++)
	{
		const std::wstring argument = argv[i];
		if (argument == L"--plugin" && i + 1 < argc)
			pluginPath = argv[++i];
		else if (argument == L"--monitor" && i + 1 < argc)
			monitorPluginPath = argv[++i];
		else if (argument == L"--seconds" && i + 1 < argc)
			seconds = _wtof(argv[++i]);
		else if (argument == L"--rate" && i + 1 < argc)
			sampleRate = static_cast<float>(_wtof(argv[++i]));
		else if (argument == L"--expect-nonsilent")
			expectNonsilent = true;
		else if (argument == L"--expect-live")
			expectLive = true;
		else if (argument == L"--expect-static")
			expectLive = false;
		else if (argument == L"--expect-audio")
			expectAudio = true;
		else if (argument == L"--expect-silence")
			expectAudio = false;
		else if (argument == L"--loopback")
			loopback = true;
		else if (argument == L"--capture-only")
			captureOnly = true;
		else
		{
			wprintf(L"%s", usage);
			return 1;
		}
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
		const int result = runPluginProbe(pluginPath, seconds, sampleRate, expectNonsilent);
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
