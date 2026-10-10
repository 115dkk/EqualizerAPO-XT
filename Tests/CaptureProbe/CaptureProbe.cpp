/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	CaptureProbe: measures what a recording application receives from a
	capture endpoint. It plays a sine into a playback endpoint (a virtual
	cable's input side) and, at the same time, records from a capture
	endpoint (the cable's output side) through the same WASAPI shared-mode
	path every recording app uses - so the audio engine builds the capture
	endpoint's APO chain for the stream, and whatever that chain does to the
	signal shows up in the numbers. With an EQ APO on the capture endpoint
	and a config that says "Preamp: -20 dB", the tone must arrive 20 dB down;
	without the APO, at unity.

	The stream category (--category communications) and raw mode (--raw)
	select the signal processing mode the engine picks for the stream, which
	is how voice-chat apps differ from a plain recorder. Each capture result
	also reports the time spent activating, initializing and starting the
	capture client, plus the wait for its first packet.

	The --watch-render-opens mode repeatedly opens and closes a silent shared
	render stream. It records slow cycles with wall-clock timestamps so a
	capture open that blocks the process loader lock can be matched to render
	opens delayed at the same time. --stop-file ends the watch early once a
	file appears, for a caller that cannot tell in advance how long its opens
	will take. This mode uses no capture endpoint and exits
	0 when at least one cycle ran, or 1 when none ran.

	Exit codes: 0 measured (or within --expect-gain-db), 1 usage or a failed
	call, 2 the measured gain missed the expectation.
*/

#define INITGUID
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct Options
{
	std::wstring renderName;
	std::wstring captureName;
	std::wstring renderId;
	std::wstring captureId;
	double seconds = 2.0;
	double settle = 0.7;
	double tone = 1000.0;
	double amplitude = 0.5;
	int category = -1;
	std::wstring categoryName = L"default";
	bool raw = false;
	bool json = false;
	bool list = false;
	bool noRender = false;
	bool watchRenderOpensSet = false;
	double watchRenderOpens = 0.0;
	std::wstring stopFile;          // --watch-render-opens ends early once this file exists
	std::wstring period;            // "", "default", "min" or a frame count: the playback stream's engine period
	bool holdDefault = false;       // hold a silent default-period playback stream open before --period opens
	bool haveExpectation = false;
	double expectGainDb = 0.0;
	double toleranceDb = 1.0;
};

void usage()
{
	fwprintf(stderr,
		L"CaptureProbe [--render <name-substring>|--render-id <id>|--no-render]\n"
		L"             [--capture <name-substring>|--capture-id <id>] [options]\n"
		L"  --list                    print the active endpoints and exit\n"
		L"  --watch-render-opens S    repeatedly open the selected render endpoint for S seconds; exit 0 if a cycle ran\n"
		L"  --stop-file PATH          with --watch-render-opens: stop as soon as PATH exists\n"
		L"  --seconds S --settle S    measure S seconds after S seconds of settling (2.0, 0.7)\n"
		L"  --tone Hz --amp A         the sine played into the playback endpoint (1000, 0.5)\n"
		L"  --category NAME           other|communications|speech|media|game-chat (stream category)\n"
		L"  --raw                     ask for the raw signal processing mode\n"
		L"  --period min|default|N    open the playback stream through IAudioClient3 at that engine period (frames)\n"
		L"  --hold-default            first hold a silent playback stream at the default period, then open --period\n"
		L"  --expect-gain-db X [--tolerance-db Y]  exit 2 unless tone gain is X +- Y\n"
		L"  --json                    one JSON line on stdout\n"
		L"Without --render/--capture the default endpoints are used.\n"
		L"Exit 3: the deadline cut the window short (the reading is still printed).\n");
}

bool parse(int argc, wchar_t** argv, Options& o)
{
	for (int i = 1; i < argc; i++)
	{
		std::wstring a = argv[i];
		auto next = [&](std::wstring& into) {
			if (i + 1 >= argc)
				return false;
			into = argv[++i];
			return true;
		};
		std::wstring v;
		if (a == L"--render" && next(v))
			o.renderName = v;
		else if (a == L"--render-id" && next(v))
			o.renderId = v;
		else if (a == L"--no-render")
			o.noRender = true;
		else if (a == L"--watch-render-opens" && next(v))
		{
			o.watchRenderOpensSet = true;
			o.watchRenderOpens = _wtof(v.c_str());
		}
		else if (a == L"--stop-file" && next(v))
			o.stopFile = v;
		else if (a == L"--capture" && next(v))
			o.captureName = v;
		else if (a == L"--capture-id" && next(v))
			o.captureId = v;
		else if (a == L"--seconds" && next(v))
			o.seconds = _wtof(v.c_str());
		else if (a == L"--settle" && next(v))
			o.settle = _wtof(v.c_str());
		else if (a == L"--tone" && next(v))
			o.tone = _wtof(v.c_str());
		else if (a == L"--amp" && next(v))
			o.amplitude = _wtof(v.c_str());
		else if (a == L"--category" && next(v))
		{
			o.categoryName = v;
			if (v == L"other")
				o.category = AudioCategory_Other;
			else if (v == L"communications")
				o.category = AudioCategory_Communications;
			else if (v == L"speech")
				o.category = AudioCategory_Speech;
			else if (v == L"media")
				o.category = AudioCategory_Media;
			else if (v == L"game-chat")
				o.category = AudioCategory_GameChat;
			else
			{
				fwprintf(stderr, L"unknown category: %s\n", v.c_str());
				return false;
			}
		}
		else if (a == L"--raw")
			o.raw = true;
		else if (a == L"--period" && next(v))
			o.period = v;
		else if (a == L"--hold-default")
			o.holdDefault = true;
		else if (a == L"--expect-gain-db" && next(v))
		{
			o.haveExpectation = true;
			o.expectGainDb = _wtof(v.c_str());
		}
		else if (a == L"--tolerance-db" && next(v))
			o.toleranceDb = _wtof(v.c_str());
		else if (a == L"--json")
			o.json = true;
		else if (a == L"--list")
			o.list = true;
		else
		{
			fwprintf(stderr, L"unknown or incomplete argument: %s\n", a.c_str());
			return false;
		}
	}
	return o.seconds > 0.0 && o.settle >= 0.0 && (!o.watchRenderOpensSet || o.watchRenderOpens > 0.0);
}

template<typename T>
struct ComRelease
{
	T* p = nullptr;
	~ComRelease()
	{
		if (p)
			p->Release();
	}
};

double elapsedMs(const LARGE_INTEGER& start, const LARGE_INTEGER& end)
{
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	return 1000.0 * static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart);
}

long long unixMs()
{
	FILETIME fileTime;
	GetSystemTimePreciseAsFileTime(&fileTime);
	ULARGE_INTEGER ticks;
	ticks.LowPart = fileTime.dwLowDateTime;
	ticks.HighPart = fileTime.dwHighDateTime;
	return static_cast<long long>(ticks.QuadPart / 10000ULL - 11644473600000ULL);
}

std::string jsonEscape(const std::wstring& text)
{
	if (text.empty())
		return {};
	const int byteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	if (byteCount <= 0)
		return "?";
	std::string utf8(static_cast<size_t>(byteCount), '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), utf8.data(), byteCount, nullptr, nullptr);
	std::string escaped;
	for (unsigned char c : utf8)
	{
		switch (c)
		{
		case '"': escaped += "\\\""; break;
		case '\\': escaped += "\\\\"; break;
		case '\b': escaped += "\\b"; break;
		case '\f': escaped += "\\f"; break;
		case '\n': escaped += "\\n"; break;
		case '\r': escaped += "\\r"; break;
		case '\t': escaped += "\\t"; break;
		default:
			if (c < 0x20)
			{
				char encoded[7] = {};
				sprintf_s(encoded, "\\u%04X", static_cast<unsigned>(c));
				escaped += encoded;
			}
			else
			{
				escaped += static_cast<char>(c);
			}
		}
	}
	return escaped;
}

std::wstring lower(std::wstring s)
{
	for (wchar_t& c : s)
		c = (wchar_t)towlower(c);
	return s;
}

std::wstring friendlyName(IMMDevice* device)
{
	ComRelease<IPropertyStore> store;
	if (FAILED(device->OpenPropertyStore(STGM_READ, &store.p)))
		return L"?";
	PROPVARIANT value;
	PropVariantInit(&value);
	std::wstring name = L"?";
	if (SUCCEEDED(store.p->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
		name = value.pwszVal;
	PropVariantClear(&value);
	return name;
}

std::wstring deviceId(IMMDevice* device)
{
	LPWSTR id = nullptr;
	std::wstring result;
	if (SUCCEEDED(device->GetId(&id)) && id)
	{
		result = id;
		CoTaskMemFree(id);
	}
	return result;
}

void listEndpoints(IMMDeviceEnumerator* enumerator)
{
	for (int flow = 0; flow < 2; flow++)
	{
		ComRelease<IMMDeviceCollection> collection;
		if (FAILED(enumerator->EnumAudioEndpoints(flow == 0 ? eRender : eCapture, DEVICE_STATE_ACTIVE, &collection.p)))
			continue;
		UINT count = 0;
		collection.p->GetCount(&count);
		for (UINT i = 0; i < count; i++)
		{
			ComRelease<IMMDevice> device;
			if (FAILED(collection.p->Item(i, &device.p)))
				continue;
			printf("%s\t%ls\t%ls\n", flow == 0 ? "render " : "capture", deviceId(device.p).c_str(), friendlyName(device.p).c_str());
		}
	}
}

// By id, by a case-insensitive substring of the friendly name, or the
// default endpoint. Only active endpoints qualify.
IMMDevice* findEndpoint(IMMDeviceEnumerator* enumerator, EDataFlow flow, const std::wstring& name, const std::wstring& id)
{
	if (!id.empty())
	{
		IMMDevice* device = nullptr;
		if (SUCCEEDED(enumerator->GetDevice(id.c_str(), &device)))
			return device;
		return nullptr;
	}
	if (name.empty())
	{
		IMMDevice* device = nullptr;
		if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device)))
			return device;
		return nullptr;
	}
	ComRelease<IMMDeviceCollection> collection;
	if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection.p)))
		return nullptr;
	UINT count = 0;
	collection.p->GetCount(&count);
	const std::wstring needle = lower(name);
	for (UINT i = 0; i < count; i++)
	{
		IMMDevice* device = nullptr;
		if (FAILED(collection.p->Item(i, &device)))
			continue;
		if (lower(friendlyName(device)).find(needle) != std::wstring::npos)
			return device;
		device->Release();
	}
	return nullptr;
}

// A float32 stream format at the endpoint's mix rate and channel count.
// Asked for with AUTOCONVERTPCM, so the engine converts if the mix format
// is something else; shared-mode mix formats are float32 anyway.
WAVEFORMATEXTENSIBLE floatFormatLike(const WAVEFORMATEX* mix)
{
	WAVEFORMATEXTENSIBLE fmt = {};
	fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	fmt.Format.nChannels = mix->nChannels;
	fmt.Format.nSamplesPerSec = mix->nSamplesPerSec;
	fmt.Format.wBitsPerSample = 32;
	fmt.Format.nBlockAlign = (WORD)(fmt.Format.nChannels * 4);
	fmt.Format.nAvgBytesPerSec = fmt.Format.nSamplesPerSec * fmt.Format.nBlockAlign;
	fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	fmt.Samples.wValidBitsPerSample = 32;
	if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		fmt.dwChannelMask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix)->dwChannelMask;
	else
		fmt.dwChannelMask = mix->nChannels == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
	fmt.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
	return fmt;
}

struct OpenCycle
{
	long long startUnixMs = 0;
	double elapsedMs = 0.0;
	HRESULT hr = S_OK;
};

OpenCycle openRenderCycle(IMMDevice* device)
{
	OpenCycle cycle;
	cycle.startUnixMs = unixMs();
	LARGE_INTEGER start, end;
	QueryPerformanceCounter(&start);
	{
		ComRelease<IAudioClient> client;
		WAVEFORMATEX* mix = nullptr;
		cycle.hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client.p));
		if (SUCCEEDED(cycle.hr))
			cycle.hr = client.p->GetMixFormat(&mix);
		if (SUCCEEDED(cycle.hr))
		{
			const WAVEFORMATEXTENSIBLE fmt = floatFormatLike(mix);
			cycle.hr = client.p->Initialize(AUDCLNT_SHAREMODE_SHARED,
				AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
				2000000, 0, &fmt.Format, nullptr);
		}
		if (SUCCEEDED(cycle.hr))
			cycle.hr = client.p->Start();
		if (SUCCEEDED(cycle.hr))
			cycle.hr = client.p->Stop();
		if (mix)
			CoTaskMemFree(mix);
	}
	QueryPerformanceCounter(&end);
	cycle.elapsedMs = elapsedMs(start, end);
	return cycle;
}

int watchRenderOpens(IMMDevice* device, const std::wstring& label, double seconds, const std::wstring& stopFile)
{
	const long long startedUnixMs = unixMs();
	LARGE_INTEGER watchStart, now;
	QueryPerformanceCounter(&watchStart);
	std::vector<OpenCycle> cycles;
	while (true)
	{
		QueryPerformanceCounter(&now);
		if (elapsedMs(watchStart, now) >= seconds * 1000.0)
			break;
		// The caller does not know in advance how long its opens will take (a
		// plugin may load for seconds or for tens of seconds), so it gives a
		// generous S and ends the watch by creating this file.
		if (!stopFile.empty() && GetFileAttributesW(stopFile.c_str()) != INVALID_FILE_ATTRIBUTES)
			break;
		OpenCycle cycle = openRenderCycle(device);
		cycles.push_back(cycle);
		if (FAILED(cycle.hr))
			fwprintf(stderr, L"render open cycle failed: 0x%08X\n", static_cast<unsigned>(cycle.hr));
		Sleep(100);
	}
	const long long endedUnixMs = unixMs();
	if (cycles.empty())
	{
		printf("{\"watch\":\"%s\",\"startedUnixMs\":%lld,\"endedUnixMs\":%lld,\"cycles\":0,\"failed\":0,\"medianMs\":0.0,\"worstMs\":0.0,\"worstAtUnixMs\":0,\"slow\":[]}\n",
			jsonEscape(label).c_str(), startedUnixMs, endedUnixMs);
		return 1;
	}

	std::vector<double> durations;
	durations.reserve(cycles.size());
	int failed = 0;
	const OpenCycle* worst = &cycles[0];
	for (const OpenCycle& cycle : cycles)
	{
		durations.push_back(cycle.elapsedMs);
		if (FAILED(cycle.hr))
			failed++;
		if (cycle.elapsedMs > worst->elapsedMs)
			worst = &cycle;
	}
	std::sort(durations.begin(), durations.end());
	const size_t middle = durations.size() / 2;
	const double median = durations.size() % 2 != 0
		? durations[middle]
		: (durations[middle - 1] + durations[middle]) / 2.0;

	printf("{\"watch\":\"%s\",\"startedUnixMs\":%lld,\"endedUnixMs\":%lld,\"cycles\":%zu,\"failed\":%d,\"medianMs\":%.1f,\"worstMs\":%.1f,\"worstAtUnixMs\":%lld,\"slow\":[",
		jsonEscape(label).c_str(), startedUnixMs, endedUnixMs, cycles.size(), failed, median, worst->elapsedMs, worst->startUnixMs);
	int slowCount = 0;
	for (const OpenCycle& cycle : cycles)
	{
		if (cycle.elapsedMs > 250.0 && slowCount < 200)
		{
			printf("%s[%lld,%.1f]", slowCount == 0 ? "" : ",", cycle.startUnixMs, cycle.elapsedMs);
			slowCount++;
		}
	}
	printf("]}\n");
	return 0;
}

struct RenderJob
{
	IMMDevice* device = nullptr;
	double tone = 1000.0;
	double amplitude = 0.5;
	std::atomic<bool> stop{false};
	std::atomic<bool> started{false};
	HRESULT hr = S_OK;
	std::wstring failure;
	unsigned rate = 0;
	unsigned channels = 0;
	std::wstring periodRequest;     // "", "default", "min" or frames (Options::period)
	bool holdDefault = false;
	unsigned defaultPeriod = 0;     // GetSharedModeEnginePeriod for the stream format, in frames
	unsigned minPeriod = 0;
	unsigned maxPeriod = 0;
	unsigned requestedPeriod = 0;   // what InitializeSharedAudioStream was asked for; 0 = plain Initialize
	unsigned enginePeriod = 0;      // GetCurrentSharedModeEnginePeriod once the tone stream runs
	unsigned holdEnginePeriod = 0;  // the same while only the held default-period stream ran
};

unsigned currentEnginePeriod(IAudioClient* client)
{
	ComRelease<IAudioClient3> client3;
	if (FAILED(client->QueryInterface(__uuidof(IAudioClient3), reinterpret_cast<void**>(&client3.p))))
		return 0;
	WAVEFORMATEX* format = nullptr;
	UINT32 period = 0;
	if (FAILED(client3.p->GetCurrentSharedModeEnginePeriod(&format, &period)))
		return 0;
	if (format)
		CoTaskMemFree(format);
	return period;
}

// A silent playback stream at the engine's default period, held open so
// that a small-period stream opened afterwards makes the engine switch a
// running graph instead of building a fresh one: the case a post-mix APO
// locked at the default frame count has to survive.
struct HeldStream
{
	ComRelease<IAudioClient> client;
	ComRelease<IAudioRenderClient> render;
	UINT32 bufferFrames = 0;

	bool open(IMMDevice* device, const WAVEFORMATEXTENSIBLE& fmt, HRESULT& hr, std::wstring& failure)
	{
		hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client.p));
		if (FAILED(hr))
		{
			failure = L"Activate(hold)";
			return false;
		}
		hr = client.p->Initialize(AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
			2000000, 0, &fmt.Format, nullptr);
		if (FAILED(hr))
		{
			failure = L"Initialize(hold)";
			return false;
		}
		hr = client.p->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render.p));
		if (FAILED(hr))
		{
			failure = L"GetService(hold)";
			return false;
		}
		client.p->GetBufferSize(&bufferFrames);
		topUp();
		hr = client.p->Start();
		if (FAILED(hr))
		{
			failure = L"Start(hold)";
			return false;
		}
		return true;
	}

	void topUp()
	{
		if (render.p == nullptr)
			return;
		UINT32 padding = 0;
		if (FAILED(client.p->GetCurrentPadding(&padding)) || padding >= bufferFrames)
			return;
		BYTE* data = nullptr;
		if (SUCCEEDED(render.p->GetBuffer(bufferFrames - padding, &data)))
			render.p->ReleaseBuffer(bufferFrames - padding, AUDCLNT_BUFFERFLAGS_SILENT);
	}

	void stop()
	{
		if (client.p)
			client.p->Stop();
	}
};

void renderThread(RenderJob* job)
{
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		job->hr = hr;
		job->failure = L"CoInitializeEx";
		job->started = true;
		return;
	}
	{
		ComRelease<IAudioClient> client;
		ComRelease<IAudioRenderClient> render;
		WAVEFORMATEX* mix = nullptr;
		auto fail = [&](const wchar_t* what, HRESULT code) {
			job->hr = code;
			job->failure = what;
			job->started = true;
		};
		hr = job->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client.p));
		if (FAILED(hr))
		{
			fail(L"Activate(render)", hr);
			CoUninitialize();
			return;
		}
		hr = client.p->GetMixFormat(&mix);
		if (FAILED(hr))
		{
			fail(L"GetMixFormat(render)", hr);
			CoUninitialize();
			return;
		}
		WAVEFORMATEXTENSIBLE fmt = floatFormatLike(mix);
		CoTaskMemFree(mix);
		job->rate = fmt.Format.nSamplesPerSec;
		job->channels = fmt.Format.nChannels;

		HeldStream held;
		if (job->holdDefault)
		{
			std::wstring what;
			if (!held.open(job->device, fmt, hr, what))
			{
				fail(what.c_str(), hr);
				CoUninitialize();
				return;
			}
			job->holdEnginePeriod = currentEnginePeriod(held.client.p);
		}

		HANDLE event = nullptr;
		if (!job->periodRequest.empty())
		{
			ComRelease<IAudioClient3> client3;
			hr = client.p->QueryInterface(__uuidof(IAudioClient3), reinterpret_cast<void**>(&client3.p));
			if (FAILED(hr))
			{
				fail(L"QueryInterface(IAudioClient3)", hr);
				held.stop();
				CoUninitialize();
				return;
			}
			UINT32 defaultPeriod = 0, fundamental = 0, minPeriod = 0, maxPeriod = 0;
			hr = client3.p->GetSharedModeEnginePeriod(&fmt.Format, &defaultPeriod, &fundamental, &minPeriod, &maxPeriod);
			if (FAILED(hr))
			{
				fail(L"GetSharedModeEnginePeriod", hr);
				held.stop();
				CoUninitialize();
				return;
			}
			job->defaultPeriod = defaultPeriod;
			job->minPeriod = minPeriod;
			job->maxPeriod = maxPeriod;
			UINT32 want = defaultPeriod;
			if (job->periodRequest == L"min")
				want = minPeriod;
			else if (job->periodRequest != L"default")
			{
				want = (UINT32)_wtoi(job->periodRequest.c_str());
				if (want < minPeriod)
					want = minPeriod;
				if (want > maxPeriod)
					want = maxPeriod;
				if (fundamental > 1)
					want = ((want + fundamental - 1) / fundamental) * fundamental;
			}
			event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			hr = client3.p->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, want, &fmt.Format, nullptr);
			if (FAILED(hr))
			{
				fail(hr == AUDCLNT_E_ENGINE_PERIODICITY_LOCKED
					? L"InitializeSharedAudioStream (another stream holds the engine at a different period)"
					: L"InitializeSharedAudioStream", hr);
				CloseHandle(event);
				held.stop();
				CoUninitialize();
				return;
			}
			job->requestedPeriod = want;
			client.p->SetEventHandle(event);
		}
		else
		{
			hr = client.p->Initialize(AUDCLNT_SHAREMODE_SHARED,
				AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
				2000000, 0, &fmt.Format, nullptr);
			if (FAILED(hr))
			{
				fail(L"Initialize(render)", hr);
				held.stop();
				CoUninitialize();
				return;
			}
		}
		UINT32 bufferFrames = 0;
		client.p->GetBufferSize(&bufferFrames);
		hr = client.p->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render.p));
		if (FAILED(hr))
		{
			fail(L"GetService(IAudioRenderClient)", hr);
			if (event)
				CloseHandle(event);
			held.stop();
			CoUninitialize();
			return;
		}

		unsigned long long sampleIndex = 0;
		auto fill = [&](UINT32 frames) {
			BYTE* data = nullptr;
			if (FAILED(render.p->GetBuffer(frames, &data)))
				return false;
			float* samples = reinterpret_cast<float*>(data);
			for (UINT32 f = 0; f < frames; f++, sampleIndex++)
			{
				const float value = (float)(job->amplitude * std::sin(2.0 * std::numbers::pi_v<double> * job->tone * (double)sampleIndex / (double)job->rate));
				for (unsigned c = 0; c < job->channels; c++)
					samples[(size_t)f * job->channels + c] = value;
			}
			render.p->ReleaseBuffer(frames, 0);
			return true;
		};

		// A small period leaves a few milliseconds per wake-up; the tone
		// thread must not lose them to a busy runner.
		if (event)
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
		fill(bufferFrames);
		hr = client.p->Start();
		if (FAILED(hr))
		{
			fail(L"Start(render)", hr);
			if (event)
				CloseHandle(event);
			held.stop();
			CoUninitialize();
			return;
		}
		job->enginePeriod = currentEnginePeriod(client.p);
		job->started = true;
		while (!job->stop)
		{
			if (event)
				WaitForSingleObject(event, 1000);
			else
				Sleep(5);
			UINT32 padding = 0;
			if (FAILED(client.p->GetCurrentPadding(&padding)))
				break;
			const UINT32 available = bufferFrames - padding;
			if (available > 0 && !fill(available))
				break;
			held.topUp();
		}
		client.p->Stop();
		held.stop();
		if (event)
			CloseHandle(event);
	}
	CoUninitialize();
}

double toDb(double amplitude)
{
	if (amplitude <= 0.0)
		return -200.0;
	return 20.0 * std::log10(amplitude);
}
}

int wmain(int argc, wchar_t** argv)
{
	Options o;
	if (!parse(argc, argv, o))
	{
		usage();
		return 1;
	}

	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		fwprintf(stderr, L"CoInitializeEx failed: 0x%08X\n", hr);
		return 1;
	}

	int result = 1;
	{
		ComRelease<IMMDeviceEnumerator> enumerator;
		hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator.p));
		if (FAILED(hr))
		{
			fwprintf(stderr, L"MMDeviceEnumerator failed: 0x%08X\n", hr);
			CoUninitialize();
			return 1;
		}
		if (o.list)
		{
			listEndpoints(enumerator.p);
			CoUninitialize();
			return 0;
		}
		if (o.watchRenderOpensSet)
		{
			IMMDevice* watchedRender = findEndpoint(enumerator.p, eRender, o.renderName, o.renderId);
			if (watchedRender == nullptr)
			{
				fwprintf(stderr, L"no active render endpoint matches\n");
			}
			else
			{
				result = watchRenderOpens(watchedRender, friendlyName(watchedRender), o.watchRenderOpens, o.stopFile);
				watchedRender->Release();
			}
			// Released before CoUninitialize: the enumerator's ComRelease would
			// otherwise run after it, on the way out of this scope, and turn a
			// run that only failed to find its endpoint into an access violation.
			enumerator.p->Release();
			enumerator.p = nullptr;
			CoUninitialize();
			return result;
		}

		ComRelease<IMMDevice> capture;
		capture.p = findEndpoint(enumerator.p, eCapture, o.captureName, o.captureId);
		if (capture.p == nullptr)
		{
			fwprintf(stderr, L"no active capture endpoint matches\n");
			CoUninitialize();
			return 1;
		}
		ComRelease<IMMDevice> render;
		if (!o.noRender)
		{
			render.p = findEndpoint(enumerator.p, eRender, o.renderName, o.renderId);
			if (render.p == nullptr)
			{
				fwprintf(stderr, L"no active render endpoint matches\n");
				CoUninitialize();
				return 1;
			}
		}
		const std::wstring captureLabel = friendlyName(capture.p);
		const std::wstring renderLabel = render.p ? friendlyName(render.p) : L"(none)";
		fwprintf(stderr, L"capture: %s\nrender:  %s\n", captureLabel.c_str(), renderLabel.c_str());

		// The capture stream first, so the APO chain is in place before the
		// tone starts; the settling time absorbs both start-ups.
		const long long openStartUnixMs = unixMs();
		LARGE_INTEGER callStart, callEnd;
		ComRelease<IAudioClient> client;
		QueryPerformanceCounter(&callStart);
		hr = capture.p->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client.p));
		QueryPerformanceCounter(&callEnd);
		const double activateMs = elapsedMs(callStart, callEnd);
		if (FAILED(hr))
		{
			fwprintf(stderr, L"Activate(capture) failed: 0x%08X\n", hr);
			CoUninitialize();
			return 1;
		}
		if (o.category >= 0 || o.raw)
		{
			ComRelease<IAudioClient2> client2;
			if (FAILED(client.p->QueryInterface(__uuidof(IAudioClient2), reinterpret_cast<void**>(&client2.p))))
			{
				fwprintf(stderr, L"IAudioClient2 is not available; cannot set the stream category\n");
				CoUninitialize();
				return 1;
			}
			AudioClientProperties props = {};
			props.cbSize = sizeof(props);
			props.bIsOffload = FALSE;
			props.eCategory = o.category >= 0 ? (AUDIO_STREAM_CATEGORY)o.category : AudioCategory_Other;
			props.Options = o.raw ? AUDCLNT_STREAMOPTIONS_RAW : AUDCLNT_STREAMOPTIONS_NONE;
			hr = client2.p->SetClientProperties(&props);
			if (FAILED(hr))
			{
				// AUDCLNT_E_RAW_MODE_UNSUPPORTED: the driver declares no raw mode
				// (legacy drivers declare no modes at all), which is a fact about
				// the endpoint, not a failure of the probe.
				if (hr == AUDCLNT_E_RAW_MODE_UNSUPPORTED)
					fwprintf(stderr, L"the endpoint's driver supports no raw mode (AUDCLNT_E_RAW_MODE_UNSUPPORTED)\n");
				else
					fwprintf(stderr, L"SetClientProperties failed: 0x%08X\n", hr);
				CoUninitialize();
				return 1;
			}
		}
		WAVEFORMATEX* mix = nullptr;
		hr = client.p->GetMixFormat(&mix);
		if (FAILED(hr))
		{
			fwprintf(stderr, L"GetMixFormat(capture) failed: 0x%08X\n", hr);
			CoUninitialize();
			return 1;
		}
		WAVEFORMATEXTENSIBLE fmt = floatFormatLike(mix);
		CoTaskMemFree(mix);
		const unsigned rate = fmt.Format.nSamplesPerSec;
		const unsigned channels = fmt.Format.nChannels;
		QueryPerformanceCounter(&callStart);
		hr = client.p->Initialize(AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
			2000000, 0, &fmt.Format, nullptr);
		QueryPerformanceCounter(&callEnd);
		const double initializeMs = elapsedMs(callStart, callEnd);
		if (FAILED(hr))
		{
			fwprintf(stderr, L"Initialize(capture) failed: 0x%08X\n", hr);
			CoUninitialize();
			return 1;
		}
		ComRelease<IAudioCaptureClient> captureClient;
		hr = client.p->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&captureClient.p));
		if (FAILED(hr))
		{
			fwprintf(stderr, L"GetService(IAudioCaptureClient) failed: 0x%08X\n", hr);
			CoUninitialize();
			return 1;
		}
		QueryPerformanceCounter(&callStart);
		hr = client.p->Start();
		QueryPerformanceCounter(&callEnd);
		const double startMs = elapsedMs(callStart, callEnd);
		const LARGE_INTEGER firstPacketStart = callEnd;
		if (FAILED(hr))
		{
			fwprintf(stderr, L"Start(capture) failed: 0x%08X\n", hr);
			CoUninitialize();
			return 1;
		}

		RenderJob job;
		std::thread renderer;
		if (render.p)
		{
			job.device = render.p;
			job.tone = o.tone;
			job.amplitude = o.amplitude;
			job.periodRequest = o.period;
			job.holdDefault = o.holdDefault;
			renderer = std::thread(renderThread, &job);
			while (!job.started)
				Sleep(5);
			if (FAILED(job.hr))
			{
				fwprintf(stderr, L"%s failed: 0x%08X\n", job.failure.c_str(), job.hr);
				job.stop = true;
				renderer.join();
				client.p->Stop();
				CoUninitialize();
				return 1;
			}
		}

		// Measure: per-channel RMS over the window, and a Goertzel bin at
		// the tone on channel 0 so a level reading cannot be faked by noise.
		const unsigned long long settleFrames = (unsigned long long)(o.settle * rate);
		const unsigned long long measureFrames = (unsigned long long)(o.seconds * rate);
		std::vector<double> sumSquares(channels, 0.0);
		unsigned long long seen = 0, counted = 0, silentPackets = 0;
		double firstPacketMs = -1.0;
		const double coeff = 2.0 * std::cos(2.0 * std::numbers::pi_v<double> * o.tone / (double)rate);
		double s1 = 0.0, s2 = 0.0;
		const DWORD deadline = GetTickCount() + (DWORD)((o.settle + o.seconds) * 1000.0) + 3000;
		HRESULT captureError = S_OK;
		// A window the deadline cut short fails the run (exit 3) after the
		// reading is printed: a gain measured over less than the requested
		// window is not the measurement that was asked for (audit #348 TD-73;
		// it used to pass).
		bool cutShort = false;
		while (counted < measureFrames)
		{
			if (GetTickCount() > deadline)
			{
				fwprintf(stderr, L"capture delivered %llu of %llu frames before the deadline\n", counted, measureFrames);
				cutShort = true;
				break;
			}
			UINT32 packet = 0;
			hr = captureClient.p->GetNextPacketSize(&packet);
			if (FAILED(hr))
			{
				captureError = hr;
				break;
			}
			if (packet > 0 && firstPacketMs < 0.0)
			{
				LARGE_INTEGER firstPacketEnd;
				QueryPerformanceCounter(&firstPacketEnd);
				firstPacketMs = elapsedMs(firstPacketStart, firstPacketEnd);
			}
			while (packet > 0 && counted < measureFrames)
			{
				BYTE* data = nullptr;
				UINT32 frames = 0;
				DWORD flags = 0;
				hr = captureClient.p->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
				if (FAILED(hr))
				{
					captureError = hr;
					break;
				}
				const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
				const float* samples = reinterpret_cast<const float*>(data);
				for (UINT32 f = 0; f < frames; f++, seen++)
				{
					if (seen < settleFrames || counted >= measureFrames)
						continue;
					for (unsigned c = 0; c < channels; c++)
					{
						const double x = silent ? 0.0 : (double)samples[(size_t)f * channels + c];
						sumSquares[c] += x * x;
						if (c == 0)
						{
							const double s0 = x + coeff * s1 - s2;
							s2 = s1;
							s1 = s0;
						}
					}
					counted++;
				}
				if (silent && seen > settleFrames)
					silentPackets++;
				captureClient.p->ReleaseBuffer(frames);
				hr = captureClient.p->GetNextPacketSize(&packet);
				if (FAILED(hr))
				{
					captureError = hr;
					break;
				}
				if (packet > 0 && firstPacketMs < 0.0)
				{
					LARGE_INTEGER firstPacketEnd;
					QueryPerformanceCounter(&firstPacketEnd);
					firstPacketMs = elapsedMs(firstPacketStart, firstPacketEnd);
				}
			}
			if (FAILED(captureError))
				break;
			Sleep(5);
		}

		client.p->Stop();
		if (renderer.joinable())
		{
			job.stop = true;
			renderer.join();
		}

		if (FAILED(captureError))
		{
			fwprintf(stderr, L"capture failed: 0x%08X\n", captureError);
		}
		else if (counted == 0)
		{
			fwprintf(stderr, L"nothing captured\n");
		}
		else
		{
			const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
			const double toneAmplitude = 2.0 * std::sqrt(power < 0.0 ? 0.0 : power) / (double)counted;
			const double toneDb = toDb(toneAmplitude / std::sqrt(2.0));
			const double expectedToneDb = toDb(o.amplitude / std::sqrt(2.0));
			const double gainDb = toneDb - expectedToneDb;
			std::vector<double> rmsDb(channels);
			for (unsigned c = 0; c < channels; c++)
				rmsDb[c] = toDb(std::sqrt(sumSquares[c] / (double)counted));

			if (o.json)
			{
				printf("{\"capture\":\"%ls\",\"render\":\"%ls\",\"category\":\"%ls\",\"raw\":%s,\"rate\":%u,\"channels\":%u,"
					"\"frames\":%llu,\"requestedFrames\":%llu,\"silentPackets\":%llu,"
					"\"renderRate\":%u,\"renderPeriodFrames\":%u,\"engineDefaultPeriodFrames\":%u,\"engineMinPeriodFrames\":%u,"
					"\"enginePeriodFrames\":%u,\"holdEnginePeriodFrames\":%u,"
					"\"activateMs\":%.1f,\"initializeMs\":%.1f,\"startMs\":%.1f,\"firstPacketMs\":%.1f,\"openStartUnixMs\":%lld,\"rmsDb\":[",
					captureLabel.c_str(), renderLabel.c_str(), o.categoryName.c_str(), o.raw ? "true" : "false",
					rate, channels, counted, measureFrames, silentPackets,
					job.rate, job.requestedPeriod, job.defaultPeriod, job.minPeriod, job.enginePeriod, job.holdEnginePeriod,
					activateMs, initializeMs, startMs, firstPacketMs, openStartUnixMs);
				for (unsigned c = 0; c < channels; c++)
					printf("%s%.2f", c ? "," : "", rmsDb[c]);
				printf("],\"toneDb\":%.2f,\"expectedToneDb\":%.2f,\"gainDb\":%.2f}\n", toneDb, expectedToneDb, gainDb);
			}
			else
			{
				printf("capture %ls <- render %ls (%ls%s) %u Hz %u ch, %llu frames, %llu silent packets\n",
					captureLabel.c_str(), renderLabel.c_str(), o.categoryName.c_str(), o.raw ? ", raw" : "", rate, channels, counted, silentPackets);
				printf("open: activate %.1f ms, initialize %.1f ms, start %.1f ms, first packet %.1f ms\n",
					activateMs, initializeMs, startMs, firstPacketMs);
				printf("rms dBFS:");
				for (unsigned c = 0; c < channels; c++)
					printf(" %.2f", rmsDb[c]);
				printf("\ntone %.0f Hz: %.2f dBFS, played at %.2f dBFS -> gain %.2f dB\n", o.tone, toneDb, expectedToneDb, gainDb);
				if (job.requestedPeriod != 0)
					printf("playback period %u frames at %u Hz (engine default %u, min %u); the engine ran at %u%s\n",
						job.requestedPeriod, job.rate, job.defaultPeriod, job.minPeriod, job.enginePeriod,
						job.holdDefault ? " after a held default-period stream" : "");
			}
			result = 0;
			if (cutShort)
			{
				fprintf(stderr, "only %llu of the %llu requested frames were measured; the reading is not valid\n",
					counted, measureFrames);
				result = 3;
			}
			else if (o.haveExpectation && std::fabs(gainDb - o.expectGainDb) > o.toleranceDb)
			{
				fprintf(stderr, "gain %.2f dB is outside %.2f +- %.2f dB\n", gainDb, o.expectGainDb, o.toleranceDb);
				result = 2;
			}
		}
	}
	CoUninitialize();
	return result;
}
