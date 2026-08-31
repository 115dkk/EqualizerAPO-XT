/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	ApoHostProbe: hosts EqualizerAPO.dll the way the audio engine does, minus
	the audio engine. It loads the DLL through its own DllGetClassObject,
	hands Initialize an APOInitSystemEffects2 whose endpoint property store
	answers PKEY_AudioEndpoint_GUID with the endpoint the caller names,
	negotiates a float connection, locks, and pushes a sine through
	APOProcess. The DLL then does what it does inside audiodg: it reads the
	endpoint's record from HKLM to learn the direction (a capture endpoint
	sets capture=true), loads the registry's ConfigPath, and filters.

	What this proves: the DLL's capture branch, end to end, against the real
	registry and the real config - without the audio service, without
	elevation, on any machine with the endpoint. What it cannot prove: that
	Windows loads the APO for that endpoint at all; the capture gate in CI
	covers that with a real virtual cable.

	Exit codes: 0 measured (or within --expect-gain-db), 1 usage or a failed
	call, 2 the measured gain missed the expectation.
*/

#define INITGUID
#include <windows.h>
#include <objbase.h>
#include <propsys.h>
#include <propidl.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <audioenginebaseapo.h>
#include <audiomediatype.h>

#include <cmath>
#include <numbers>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "services/registry/RegistryPaths.h"

namespace
{

struct Options
{
	std::wstring dllPath;
	std::wstring endpointGuid;
	bool preMix = true;
	unsigned sampleRate = 48000;
	unsigned inputChannels = 2;
	unsigned outputChannels = 2;
	bool float64 = false;
	unsigned frames = 480;
	unsigned blocks = 20;
	unsigned skipBlocks = 2;
	double tone = 1000.0;
	double amplitude = 0.5;
	bool json = false;
	bool haveExpectation = false;
	double expectGainDb = 0.0;
	double toleranceDb = 0.5;
};

void usage()
{
	fwprintf(stderr,
		L"ApoHostProbe --dll <EqualizerAPO.dll> --endpoint {guid} [options]\n"
		L"  --stage premix|postmix   which CLSID to create (default premix, the capture slot)\n"
		L"  --rate N --in N --out N  connection format (default 48000, 2, 2)\n"
		L"  --float64                connection sample type double instead of float\n"
		L"  --frames N --blocks N    block size and block count (default 480, 20)\n"
		L"  --tone Hz --amp A        the sine pushed through (default 1000, 0.5)\n"
		L"  --expect-gain-db X [--tolerance-db Y]  exit 2 unless the output/input gain is X +- Y\n"
		L"  --json                   one JSON line on stdout\n");
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
		if (a == L"--dll" && next(v))
			o.dllPath = v;
		else if (a == L"--endpoint" && next(v))
			o.endpointGuid = v;
		else if (a == L"--stage" && next(v))
			o.preMix = (v != L"postmix");
		else if (a == L"--rate" && next(v))
			o.sampleRate = (unsigned)_wtoi(v.c_str());
		else if (a == L"--in" && next(v))
			o.inputChannels = (unsigned)_wtoi(v.c_str());
		else if (a == L"--out" && next(v))
			o.outputChannels = (unsigned)_wtoi(v.c_str());
		else if (a == L"--float64")
			o.float64 = true;
		else if (a == L"--frames" && next(v))
			o.frames = (unsigned)_wtoi(v.c_str());
		else if (a == L"--blocks" && next(v))
			o.blocks = (unsigned)_wtoi(v.c_str());
		else if (a == L"--skip" && next(v))
			o.skipBlocks = (unsigned)_wtoi(v.c_str());
		else if (a == L"--tone" && next(v))
			o.tone = _wtof(v.c_str());
		else if (a == L"--amp" && next(v))
			o.amplitude = _wtof(v.c_str());
		else if (a == L"--expect-gain-db" && next(v))
		{
			o.haveExpectation = true;
			o.expectGainDb = _wtof(v.c_str());
		}
		else if (a == L"--tolerance-db" && next(v))
			o.toleranceDb = _wtof(v.c_str());
		else if (a == L"--json")
			o.json = true;
		else
		{
			fwprintf(stderr, L"unknown or incomplete argument: %s\n", a.c_str());
			return false;
		}
	}
	if (o.dllPath.empty() || o.endpointGuid.empty() || o.inputChannels == 0 || o.outputChannels == 0
		|| o.frames == 0 || o.blocks <= o.skipBlocks)
		return false;
	return true;
}

// The one property the DLL asks the endpoint store for. Everything else is
// VT_EMPTY, which is what a real store answers for a key it does not hold.
class EndpointPropertyStore : public IPropertyStore
{
public:
	explicit EndpointPropertyStore(const std::wstring& guid)
		: guid(guid)
	{
	}

	HRESULT __stdcall QueryInterface(REFIID iid, void** ppv) override
	{
		if (ppv == nullptr)
			return E_POINTER;
		if (iid == __uuidof(IUnknown) || iid == __uuidof(IPropertyStore))
		{
			*ppv = static_cast<IPropertyStore*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	ULONG __stdcall AddRef() override
	{
		return InterlockedIncrement(&refs);
	}

	ULONG __stdcall Release() override
	{
		const LONG remaining = InterlockedDecrement(&refs);
		if (remaining == 0)
			delete this;
		return (ULONG)remaining;
	}

	HRESULT __stdcall GetCount(DWORD* count) override
	{
		if (count == nullptr)
			return E_POINTER;
		*count = 1;
		return S_OK;
	}

	HRESULT __stdcall GetAt(DWORD index, PROPERTYKEY* key) override
	{
		if (key == nullptr)
			return E_POINTER;
		if (index != 0)
			return E_INVALIDARG;
		*key = PKEY_AudioEndpoint_GUID;
		return S_OK;
	}

	HRESULT __stdcall GetValue(REFPROPERTYKEY key, PROPVARIANT* value) override
	{
		if (value == nullptr)
			return E_POINTER;
		PropVariantInit(value);
		if (IsEqualPropertyKey(key, PKEY_AudioEndpoint_GUID))
		{
			const size_t bytes = (guid.size() + 1) * sizeof(wchar_t);
			wchar_t* copy = static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
			if (copy == nullptr)
				return E_OUTOFMEMORY;
			memcpy(copy, guid.c_str(), bytes);
			value->vt = VT_LPWSTR;
			value->pwszVal = copy;
		}
		return S_OK;
	}

	HRESULT __stdcall SetValue(REFPROPERTYKEY, REFPROPVARIANT) override
	{
		return STG_E_ACCESSDENIED;
	}

	HRESULT __stdcall Commit() override
	{
		return S_OK;
	}

private:
	std::wstring guid;
	LONG refs = 1;
};

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

std::wstring apoLogPath()
{
	wchar_t temp[MAX_PATH + 1] = {};
	if (GetTempPathW(MAX_PATH, temp) == 0)
		return std::wstring();
	return std::wstring(temp) + L"EqualizerAPO.log";
}

long long fileSize(const std::wstring& path)
{
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
		return 0;
	return ((long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

// Prints what the DLL appended to its log while the probe ran; the file is
// the one place the DLL explains itself (endpoint record not readable,
// config path missing, format refused).
void printLogTail(const std::wstring& path, long long from)
{
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr)
		return;
	_fseeki64(fp, from, SEEK_SET);
	std::string chunk;
	char buffer[4096];
	size_t got;
	while ((got = fread(buffer, 1, sizeof(buffer), fp)) > 0)
		chunk.append(buffer, got);
	fclose(fp);
	if (chunk.empty())
		return;
	size_t start = 0;
	while (start < chunk.size())
	{
		size_t end = chunk.find('\n', start);
		if (end == std::string::npos)
			end = chunk.size();
		std::string line = chunk.substr(start, end - start);
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
			line.pop_back();
		if (!line.empty())
			fprintf(stderr, "apo-log: %s\n", line.c_str());
		start = end + 1;
	}
}

double toDb(double ratio)
{
	if (ratio <= 0.0)
		return -200.0;
	return 20.0 * std::log10(ratio);
}

HRESULT makeMediaType(const Options& o, unsigned channels, IAudioMediaType** type)
{
	UNCOMPRESSEDAUDIOFORMAT format = {};
	format.guidFormatType = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
	format.dwSamplesPerFrame = channels;
	format.dwBytesPerSampleContainer = o.float64 ? 8 : 4;
	format.dwValidBitsPerSample = o.float64 ? 64 : 32;
	format.fFramesPerSecond = (FLOAT)o.sampleRate;
	format.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER
		: channels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
	return CreateAudioMediaTypeFromUncompressedAudioFormat(&format, type);
}

template<typename SampleT>
int run(const Options& o, IAudioProcessingObjectRT* rt,
	APO_CONNECTION_PROPERTY* inProp, APO_CONNECTION_PROPERTY* outProp,
	SampleT* input, SampleT* output, double& gainDb, double& residualDb, double& rmsIn, double& rmsOut)
{
	double sumIn = 0.0, sumOut = 0.0, sumDiff = 0.0;
	unsigned long long counted = 0;
	unsigned long long sampleIndex = 0;
	for (unsigned block = 0; block < o.blocks; block++)
	{
		for (unsigned f = 0; f < o.frames; f++, sampleIndex++)
		{
			const double value = o.amplitude * std::sin(2.0 * std::numbers::pi_v<double> * o.tone * (double)sampleIndex / (double)o.sampleRate);
			for (unsigned c = 0; c < o.inputChannels; c++)
				input[(size_t)f * o.inputChannels + c] = (SampleT)value;
		}
		memset(output, 0, (size_t)o.frames * o.outputChannels * sizeof(SampleT));
		inProp->u32ValidFrameCount = o.frames;
		inProp->u32BufferFlags = BUFFER_VALID;
		outProp->u32ValidFrameCount = 0;
		outProp->u32BufferFlags = BUFFER_INVALID;

		APO_CONNECTION_PROPERTY* ins[1] = {inProp};
		APO_CONNECTION_PROPERTY* outs[1] = {outProp};
		rt->APOProcess(1, ins, 1, outs);

		if (block < o.skipBlocks)
			continue;
		if (outProp->u32ValidFrameCount != o.frames)
		{
			fwprintf(stderr, L"block %u: APOProcess reported %u valid frames, expected %u\n",
				block, outProp->u32ValidFrameCount, o.frames);
			return 1;
		}
		// Compare channel 0 against channel 0: the probe feeds every input
		// channel the same sine, so this holds for any in/out channel pair.
		for (unsigned f = 0; f < o.frames; f++)
		{
			const double in = (double)input[(size_t)f * o.inputChannels];
			const double out = (double)output[(size_t)f * o.outputChannels];
			sumIn += in * in;
			sumOut += out * out;
			sumDiff += (out - in) * (out - in);
			counted++;
		}
	}
	rmsIn = std::sqrt(sumIn / (double)counted);
	rmsOut = std::sqrt(sumOut / (double)counted);
	gainDb = toDb(rmsOut / rmsIn);
	residualDb = toDb(std::sqrt(sumDiff / (double)counted) / rmsIn);
	return 0;
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

	const std::wstring logPath = apoLogPath();
	const long long logStart = fileSize(logPath);

	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		fwprintf(stderr, L"CoInitializeEx failed: 0x%08X\n", hr);
		return 1;
	}

	int result = 1;
	{
		// Dependencies (fftw, sndfile, muparserx) sit beside the DLL, as in
		// the install folder.
		HMODULE module = LoadLibraryExW(o.dllPath.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (module == nullptr)
		{
			fwprintf(stderr, L"LoadLibrary(%s) failed: %lu\n", o.dllPath.c_str(), GetLastError());
			CoUninitialize();
			return 1;
		}

		typedef HRESULT (__stdcall * GetClassObjectFn)(REFCLSID, REFIID, void**);
		GetClassObjectFn getClassObject = reinterpret_cast<GetClassObjectFn>(
			reinterpret_cast<void*>(GetProcAddress(module, "DllGetClassObject")));
		if (getClassObject == nullptr)
		{
			fwprintf(stderr, L"the DLL exports no DllGetClassObject\n");
			CoUninitialize();
			return 1;
		}

		const GUID clsid = o.preMix ? EQUALIZERAPO_PRE_MIX_GUID : EQUALIZERAPO_POST_MIX_GUID;
		ComRelease<IClassFactory> factory;
		hr = getClassObject(clsid, __uuidof(IClassFactory), reinterpret_cast<void**>(&factory.p));
		if (FAILED(hr))
		{
			fwprintf(stderr, L"DllGetClassObject failed: 0x%08X\n", hr);
			CoUninitialize();
			return 1;
		}

		ComRelease<IAudioProcessingObject> apo;
		hr = factory.p->CreateInstance(nullptr, __uuidof(IAudioProcessingObject), reinterpret_cast<void**>(&apo.p));
		if (FAILED(hr))
		{
			fwprintf(stderr, L"CreateInstance failed: 0x%08X\n", hr);
			CoUninitialize();
			return 1;
		}
		ComRelease<IAudioProcessingObjectRT> rt;
		ComRelease<IAudioProcessingObjectConfiguration> cfg;
		if (FAILED(apo.p->QueryInterface(__uuidof(IAudioProcessingObjectRT), reinterpret_cast<void**>(&rt.p)))
			|| FAILED(apo.p->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration), reinterpret_cast<void**>(&cfg.p))))
		{
			fwprintf(stderr, L"the APO does not expose RT/Configuration interfaces\n");
			CoUninitialize();
			return 1;
		}

		ComRelease<EndpointPropertyStore> store;
		store.p = new EndpointPropertyStore(o.endpointGuid);

		APOInitSystemEffects2 init = {};
		init.APOInit.cbSize = sizeof(init);
		init.APOInit.clsid = clsid;
		init.pAPOEndpointProperties = store.p;
		init.pAPOSystemEffectsProperties = nullptr;
		init.pReserved = nullptr;
		init.pDeviceCollection = nullptr;
		init.nSoftwareIoDeviceInCollection = 0;
		init.nSoftwareIoConnectorIndex = 0;
		init.AudioProcessingMode = AUDIO_SIGNALPROCESSINGMODE_DEFAULT;
		init.InitializeForDiscoveryOnly = FALSE;

		hr = apo.p->Initialize(sizeof(init), reinterpret_cast<BYTE*>(&init));
		fwprintf(stderr, L"Initialize: 0x%08X\n", hr);
		if (FAILED(hr))
		{
			printLogTail(logPath, logStart);
			CoUninitialize();
			return 1;
		}

		ComRelease<IAudioMediaType> inType;
		ComRelease<IAudioMediaType> outType;
		if (FAILED(makeMediaType(o, o.inputChannels, &inType.p)) || FAILED(makeMediaType(o, o.outputChannels, &outType.p)))
		{
			fwprintf(stderr, L"CreateAudioMediaTypeFromUncompressedAudioFormat failed\n");
			CoUninitialize();
			return 1;
		}

		ComRelease<IAudioMediaType> suggested;
		hr = apo.p->IsInputFormatSupported(outType.p, inType.p, &suggested.p);
		fwprintf(stderr, L"IsInputFormatSupported: 0x%08X%s\n", hr, hr == S_FALSE ? L" (another input format was suggested)" : L"");
		if (FAILED(hr))
		{
			printLogTail(logPath, logStart);
			CoUninitialize();
			return 1;
		}

		const size_t sampleBytes = o.float64 ? sizeof(double) : sizeof(float);
		void* input = _aligned_malloc((size_t)o.frames * o.inputChannels * sampleBytes, 64);
		void* output = _aligned_malloc((size_t)o.frames * o.outputChannels * sampleBytes, 64);
		if (input == nullptr || output == nullptr)
		{
			fwprintf(stderr, L"out of memory\n");
			CoUninitialize();
			return 1;
		}

		APO_CONNECTION_DESCRIPTOR inDesc = {};
		inDesc.Type = APO_CONNECTION_BUFFER_TYPE_EXTERNAL;
		inDesc.pBuffer = reinterpret_cast<UINT_PTR>(input);
		inDesc.pFormat = inType.p;
		inDesc.u32MaxFrameCount = o.frames;
		inDesc.u32Signature = APO_CONNECTION_DESCRIPTOR_SIGNATURE;
		APO_CONNECTION_DESCRIPTOR outDesc = inDesc;
		outDesc.pBuffer = reinterpret_cast<UINT_PTR>(output);
		outDesc.pFormat = outType.p;
		APO_CONNECTION_DESCRIPTOR* inDescs[1] = {&inDesc};
		APO_CONNECTION_DESCRIPTOR* outDescs[1] = {&outDesc};

		hr = cfg.p->LockForProcess(1, inDescs, 1, outDescs);
		fwprintf(stderr, L"LockForProcess: 0x%08X\n", hr);
		if (FAILED(hr))
		{
			printLogTail(logPath, logStart);
			_aligned_free(input);
			_aligned_free(output);
			CoUninitialize();
			return 1;
		}

		APO_CONNECTION_PROPERTY inProp = {};
		inProp.pBuffer = reinterpret_cast<UINT_PTR>(input);
		inProp.u32Signature = APO_CONNECTION_PROPERTY_SIGNATURE;
		APO_CONNECTION_PROPERTY outProp = inProp;
		outProp.pBuffer = reinterpret_cast<UINT_PTR>(output);

		double gainDb = 0.0, residualDb = 0.0, rmsIn = 0.0, rmsOut = 0.0;
		if (o.float64)
			result = run<double>(o, rt.p, &inProp, &outProp, static_cast<double*>(input), static_cast<double*>(output), gainDb, residualDb, rmsIn, rmsOut);
		else
			result = run<float>(o, rt.p, &inProp, &outProp, static_cast<float*>(input), static_cast<float*>(output), gainDb, residualDb, rmsIn, rmsOut);

		cfg.p->UnlockForProcess();
		_aligned_free(input);
		_aligned_free(output);

		if (result == 0)
		{
			// "changed" means the APO did something to the signal: the output
			// differs from the input by more than float noise. A passthrough
			// leaves the residual at the noise floor.
			const bool changed = residualDb > -120.0;
			if (o.json)
			{
				printf("{\"endpoint\":\"%ls\",\"stage\":\"%s\",\"rate\":%u,\"in\":%u,\"out\":%u,\"format\":\"%s\","
					"\"rmsIn\":%.6f,\"rmsOut\":%.6f,\"gainDb\":%.3f,\"residualDb\":%.1f,\"changed\":%s}\n",
					o.endpointGuid.c_str(), o.preMix ? "premix" : "postmix", o.sampleRate, o.inputChannels, o.outputChannels,
					o.float64 ? "float64" : "float32", rmsIn, rmsOut, gainDb, residualDb, changed ? "true" : "false");
			}
			else
			{
				printf("endpoint %ls stage %s %u Hz %u->%u %s\n", o.endpointGuid.c_str(), o.preMix ? "premix" : "postmix",
					o.sampleRate, o.inputChannels, o.outputChannels, o.float64 ? "float64" : "float32");
				printf("rms in %.6f out %.6f gain %.3f dB residual %.1f dB -> %s\n",
					rmsIn, rmsOut, gainDb, residualDb, changed ? "the APO changed the signal" : "passthrough");
			}
			if (o.haveExpectation && std::fabs(gainDb - o.expectGainDb) > o.toleranceDb)
			{
				fprintf(stderr, "gain %.3f dB is outside %.3f +- %.3f dB\n", gainDb, o.expectGainDb, o.toleranceDb);
				result = 2;
			}
		}
		printLogTail(logPath, logStart);
	}
	CoUninitialize();
	return result;
}
