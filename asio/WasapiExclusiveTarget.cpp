/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/WasapiExclusiveTarget.h"

#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace eapo::asio
{
	namespace wasapi
	{
		namespace
		{
			const Container float32Container = {ASIOSTFloat32LSB, 32, 32, true};
			const Container int32Container = {ASIOSTInt32LSB, 32, 32, false};
			const Container int24In32Container = {ASIOSTInt32LSB24, 32, 24, false};
			const Container int24Container = {ASIOSTInt24LSB, 24, 24, false};
			const Container int16Container = {ASIOSTInt16LSB, 16, 16, false};

			bool sameContainer(const Container& a, const Container& b) noexcept
			{
				return a.asioType == b.asioType;
			}

			// The container a WAVEFORMATEX describes, when it is one of ours.
			bool containerOf(const WAVEFORMATEX* format, Container& out) noexcept
			{
				if (format == nullptr)
					return false;
				bool isFloat = false;
				unsigned validBits = format->wBitsPerSample;
				if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22)
				{
					const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
					if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
						isFloat = true;
					else if (ext->SubFormat != KSDATAFORMAT_SUBTYPE_PCM)
						return false;
					if (ext->Samples.wValidBitsPerSample != 0)
						validBits = ext->Samples.wValidBitsPerSample;
				}
				else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
					isFloat = true;
				else if (format->wFormatTag != WAVE_FORMAT_PCM)
					return false;

				if (isFloat)
				{
					if (format->wBitsPerSample != 32)
						return false;
					out = float32Container;
					return true;
				}
				switch (format->wBitsPerSample)
				{
				case 32:
					out = validBits >= 32 ? int32Container : int24In32Container;
					return true;
				case 24:
					out = int24Container;
					return true;
				case 16:
					out = int16Container;
					return true;
				default:
					return false;
				}
			}
		}

		std::vector<Container> containerCandidates(const WAVEFORMATEX* deviceFormat)
		{
			std::vector<Container> list;
			Container own;
			if (containerOf(deviceFormat, own))
				list.push_back(own);
			for (const Container* candidate : {&float32Container, &int32Container, &int24In32Container, &int24Container, &int16Container})
			{
				bool present = false;
				for (const Container& have : list)
					present = present || sameContainer(have, *candidate);
				if (!present)
					list.push_back(*candidate);
			}
			return list;
		}

		WAVEFORMATEXTENSIBLE makeFormat(const Container& container, unsigned channels, unsigned rate, unsigned long channelMask)
		{
			WAVEFORMATEXTENSIBLE fmt = {};
			fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
			fmt.Format.nChannels = static_cast<WORD>(channels);
			fmt.Format.nSamplesPerSec = rate;
			fmt.Format.wBitsPerSample = static_cast<WORD>(container.bits);
			fmt.Format.nBlockAlign = static_cast<WORD>(channels * container.bytes());
			fmt.Format.nAvgBytesPerSec = rate * fmt.Format.nBlockAlign;
			fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
			fmt.Samples.wValidBitsPerSample = static_cast<WORD>(container.validBits);
			fmt.dwChannelMask = static_cast<DWORD>(channelMask);
			fmt.SubFormat = container.isFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
			return fmt;
		}

		BufferPolicy bufferPolicy(unsigned minPeriodFrames)
		{
			BufferPolicy policy;
			unsigned size = 32;
			while (size < minPeriodFrames && size < (1u << 20))
				size *= 2;
			policy.minSize = static_cast<long>(size);
			policy.maxSize = static_cast<long>(size > 2048 ? size : 2048);
			policy.preferredSize = static_cast<long>(size);
			policy.granularity = -1;
			return policy;
		}

		unsigned framesFromHns(long long hns, unsigned rate)
		{
			if (hns <= 0 || rate == 0)
				return 0;
			// Nearest, the way the audio stack turns a period back into
			// frames: 256 frames at 96 kHz is 26667 hns and must come back
			// as 256, not 257.
			return static_cast<unsigned>((hns * static_cast<long long>(rate) + 5000000LL) / 10000000LL);
		}

		long long hnsFromFrames(unsigned frames, unsigned rate)
		{
			if (rate == 0)
				return 0;
			return (static_cast<long long>(frames) * 10000000LL + rate / 2) / static_cast<long long>(rate);
		}

		void interleave(const void* const* planes, unsigned channels, unsigned bytesPerSample, unsigned frames, void* block)
		{
			unsigned char* out = static_cast<unsigned char*>(block);
			if (bytesPerSample == 4)
			{
				uint32_t* o = reinterpret_cast<uint32_t*>(out);
				for (unsigned c = 0; c < channels; c++)
				{
					const uint32_t* in = static_cast<const uint32_t*>(planes[c]);
					for (unsigned f = 0; f < frames; f++)
						o[static_cast<size_t>(f) * channels + c] = in[f];
				}
				return;
			}
			if (bytesPerSample == 2)
			{
				uint16_t* o = reinterpret_cast<uint16_t*>(out);
				for (unsigned c = 0; c < channels; c++)
				{
					const uint16_t* in = static_cast<const uint16_t*>(planes[c]);
					for (unsigned f = 0; f < frames; f++)
						o[static_cast<size_t>(f) * channels + c] = in[f];
				}
				return;
			}
			for (unsigned c = 0; c < channels; c++)
			{
				const unsigned char* in = static_cast<const unsigned char*>(planes[c]);
				for (unsigned f = 0; f < frames; f++)
					std::memcpy(out + (static_cast<size_t>(f) * channels + c) * bytesPerSample, in + static_cast<size_t>(f) * bytesPerSample, bytesPerSample);
			}
		}

		void deinterleave(const void* block, unsigned channels, unsigned bytesPerSample, unsigned frames, void* const* planes)
		{
			const unsigned char* in = static_cast<const unsigned char*>(block);
			if (bytesPerSample == 4)
			{
				const uint32_t* i = reinterpret_cast<const uint32_t*>(in);
				for (unsigned c = 0; c < channels; c++)
				{
					uint32_t* out = static_cast<uint32_t*>(planes[c]);
					for (unsigned f = 0; f < frames; f++)
						out[f] = i[static_cast<size_t>(f) * channels + c];
				}
				return;
			}
			if (bytesPerSample == 2)
			{
				const uint16_t* i = reinterpret_cast<const uint16_t*>(in);
				for (unsigned c = 0; c < channels; c++)
				{
					uint16_t* out = static_cast<uint16_t*>(planes[c]);
					for (unsigned f = 0; f < frames; f++)
						out[f] = i[static_cast<size_t>(f) * channels + c];
				}
				return;
			}
			for (unsigned c = 0; c < channels; c++)
			{
				unsigned char* out = static_cast<unsigned char*>(planes[c]);
				for (unsigned f = 0; f < frames; f++)
					std::memcpy(out + static_cast<size_t>(f) * bytesPerSample, in + (static_cast<size_t>(f) * channels + c) * bytesPerSample, bytesPerSample);
			}
		}

		std::wstring endpointId(bool capture, const std::wstring& endpointGuid)
		{
			return std::wstring(capture ? L"{0.0.1.00000000}." : L"{0.0.0.00000000}.") + endpointGuid;
		}
	}

	namespace
	{
		// PKEY_AudioEngine_DeviceFormat ({F19F064D-082C-4E27-BC73-6882A1BB8E4C},0):
		// the format the Sound settings show for the endpoint. Spelled out
		// because the SDK only defines the key for INITGUID translation units.
		const PROPERTYKEY keyDeviceFormat = {{0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}}, 0};

		template<typename T>
		void releaseAndNull(T*& p) noexcept
		{
			if (p != nullptr)
			{
				p->Release();
				p = nullptr;
			}
		}

		void splitInt64(uint64_t value, unsigned long& hi, unsigned long& lo) noexcept
		{
			hi = static_cast<unsigned long>(value >> 32);
			lo = static_cast<unsigned long>(value & 0xffffffffu);
		}

		uint64_t nowNanoseconds() noexcept
		{
			LARGE_INTEGER frequency, counter;
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&counter);
			const double seconds = static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
			return static_cast<uint64_t>(seconds * 1e9);
		}

		// The endpoint's name in the 32 bytes ASIO gives a driver name,
		// non-ASCII folded to '?'.
		void narrowName(const std::wstring& wide, char* out, size_t capacity) noexcept
		{
			size_t n = 0;
			for (; n + 1 < capacity && n < wide.size(); n++)
				out[n] = wide[n] < 128 ? static_cast<char>(wide[n]) : '?';
			out[n] = '\0';
		}

		const char* describe(HRESULT hr) noexcept
		{
			switch (hr)
			{
			case AUDCLNT_E_DEVICE_IN_USE:
				return "another application holds the device in exclusive mode";
			case AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED:
				return "Windows does not allow exclusive mode on this device (Sound settings, Advanced)";
			case AUDCLNT_E_UNSUPPORTED_FORMAT:
				return "the device accepts no exclusive-mode format at this rate";
			case AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED:
				return "the device needs another buffer size";
			case AUDCLNT_E_DEVICE_INVALIDATED:
				return "the device went away";
			case AUDCLNT_E_ENDPOINT_CREATE_FAILED:
				return "the endpoint could not be created";
			default:
				return "WASAPI refused the stream";
			}
		}
	}

	// ---- Port ----

	void WasapiExclusiveTarget::Port::closeStream() noexcept
	{
		if (client != nullptr)
			client->Stop();
		releaseAndNull(render);
		releaseAndNull(captureClient);
		if (event != nullptr)
		{
			CloseHandle(event);
			event = nullptr;
		}
		// An IAudioClient cannot be initialized twice; a fresh one is
		// activated so the next createBuffers can open the endpoint again.
		releaseAndNull(client);
		if (device != nullptr)
			device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
		block.clear();
		pending.clear();
		pendingFrames = 0;
		staged = 0;
		latencyFrames = 0;
	}

	// The ASIO buffers outlive a stream: the host holds their pointers from
	// createBuffers to disposeBuffers, across a rebridge that reopens the
	// device with another period.
	void WasapiExclusiveTarget::Port::releasePlanes() noexcept
	{
		planes[0].clear();
		planes[1].clear();
	}

	void WasapiExclusiveTarget::Port::closeDevice() noexcept
	{
		closeStream();
		releasePlanes();
		releaseAndNull(client);
		releaseAndNull(device);
	}

	// ---- lifetime ----

	WasapiExclusiveTarget::WasapiExclusiveTarget(std::wstring renderEndpointGuid, std::wstring captureEndpointGuid)
	{
		ports_[0].capture = false;
		ports_[0].endpointGuid = std::move(renderEndpointGuid);
		ports_[1].capture = true;
		ports_[1].endpointGuid = std::move(captureEndpointGuid);
	}

	WasapiExclusiveTarget::~WasapiExclusiveTarget()
	{
		stop();
		for (Port& port : ports_)
			port.closeDevice();
	}

	HRESULT STDMETHODCALLTYPE WasapiExclusiveTarget::QueryInterface(REFIID riid, void** object)
	{
		if (object == nullptr)
			return E_POINTER;
		if (riid == IID_IUnknown)
		{
			*object = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE WasapiExclusiveTarget::AddRef()
	{
		return static_cast<ULONG>(++refCount_);
	}

	ULONG STDMETHODCALLTYPE WasapiExclusiveTarget::Release()
	{
		const long remaining = --refCount_;
		if (remaining == 0)
		{
			delete this;
			return 0;
		}
		return static_cast<ULONG>(remaining);
	}

	void WasapiExclusiveTarget::setError(const char* message) noexcept
	{
		std::snprintf(errorMessage_, sizeof(errorMessage_), "%s", message);
	}

	WasapiExclusiveTarget::Counters WasapiExclusiveTarget::counters() const noexcept
	{
		return counters_;
	}

	// ---- init ----

	bool WasapiExclusiveTarget::openPort(Port& port, IMMDeviceEnumerator* enumerator, char* message)
	{
		const std::wstring id = wasapi::endpointId(port.capture, port.endpointGuid);
		HRESULT hr = enumerator->GetDevice(id.c_str(), &port.device);
		if (FAILED(hr) || port.device == nullptr)
		{
			std::snprintf(message, 124, "The %s endpoint is not present", port.capture ? "recording" : "playback");
			return false;
		}
		DWORD state = 0;
		if (SUCCEEDED(port.device->GetState(&state)) && state != DEVICE_STATE_ACTIVE)
		{
			std::snprintf(message, 124, "The %s endpoint is not active", port.capture ? "recording" : "playback");
			return false;
		}

		IPropertyStore* store = nullptr;
		if (SUCCEEDED(port.device->OpenPropertyStore(STGM_READ, &store)) && store != nullptr)
		{
			PROPVARIANT value;
			PropVariantInit(&value);
			if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr)
				port.friendlyName = value.pwszVal;
			PropVariantClear(&value);
			PropVariantInit(&value);
			if (SUCCEEDED(store->GetValue(keyDeviceFormat, &value)) && value.vt == VT_BLOB
				&& value.blob.cbSize >= sizeof(WAVEFORMATEX) && value.blob.pBlobData != nullptr)
			{
				port.deviceFormat.assign(value.blob.pBlobData, value.blob.pBlobData + value.blob.cbSize);
			}
			PropVariantClear(&value);
			store->Release();
		}

		hr = port.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&port.client));
		if (FAILED(hr) || port.client == nullptr)
		{
			std::snprintf(message, 124, "The %s endpoint gave no audio client (0x%08lx)", port.capture ? "recording" : "playback", static_cast<unsigned long>(hr));
			return false;
		}
		if (port.deviceFormat.empty())
		{
			WAVEFORMATEX* mix = nullptr;
			if (SUCCEEDED(port.client->GetMixFormat(&mix)) && mix != nullptr)
			{
				const unsigned char* bytes = reinterpret_cast<const unsigned char*>(mix);
				port.deviceFormat.assign(bytes, bytes + sizeof(WAVEFORMATEX) + mix->cbSize);
				CoTaskMemFree(mix);
			}
		}
		if (port.deviceFormat.empty())
		{
			std::snprintf(message, 124, "The %s endpoint reports no format", port.capture ? "recording" : "playback");
			return false;
		}
		const WAVEFORMATEX* format = reinterpret_cast<const WAVEFORMATEX*>(port.deviceFormat.data());
		port.channels = format->nChannels;
		port.deviceRate = format->nSamplesPerSec;
		if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22)
			port.channelMask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->dwChannelMask;
		else
			port.channelMask = port.channels == 1 ? SPEAKER_FRONT_CENTER : (port.channels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0);
		if (port.channels == 0 || port.deviceRate == 0)
		{
			std::snprintf(message, 124, "The %s endpoint reports no channels or rate", port.capture ? "recording" : "playback");
			return false;
		}

		REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
		if (SUCCEEDED(port.client->GetDevicePeriod(&defaultPeriod, &minPeriod)))
			port.minPeriodFrames = wasapi::framesFromHns(minPeriod, port.deviceRate);
		if (port.minPeriodFrames == 0)
			port.minPeriodFrames = 128;
		return true;
	}

	bool WasapiExclusiveTarget::negotiate(Port& port, unsigned rate, wasapi::Container* found) const
	{
		if (port.client == nullptr)
			return false;
		const WAVEFORMATEX* deviceFormat = port.deviceFormat.empty() ? nullptr : reinterpret_cast<const WAVEFORMATEX*>(port.deviceFormat.data());
		for (const wasapi::Container& candidate : wasapi::containerCandidates(deviceFormat))
		{
			WAVEFORMATEXTENSIBLE fmt = wasapi::makeFormat(candidate, port.channels, rate, port.channelMask);
			if (port.client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &fmt.Format, nullptr) == S_OK)
			{
				if (found != nullptr)
					*found = candidate;
				return true;
			}
		}
		return false;
	}

	ASIOBool WasapiExclusiveTarget::init(void* /*sysHandle*/)
	{
		if (initialized_)
			return ASIOTrue;
		if (ports_[0].endpointGuid.empty() && ports_[1].endpointGuid.empty())
		{
			setError("No endpoint is recorded for this entry");
			return ASIOFalse;
		}
		IMMDeviceEnumerator* enumerator = nullptr;
		HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
		if (FAILED(hr) || enumerator == nullptr)
		{
			setError("The audio device enumerator is not available");
			return ASIOFalse;
		}
		char message[124] = {};
		for (Port& port : ports_)
		{
			if (port.endpointGuid.empty())
				continue;
			if (!openPort(port, enumerator, message))
			{
				enumerator->Release();
				for (Port& opened : ports_)
					opened.closeDevice();
				setError(message);
				return ASIOFalse;
			}
		}
		enumerator->Release();

		rate_ = ports_[0].device != nullptr ? ports_[0].deviceRate : ports_[1].deviceRate;
		for (Port& port : ports_)
		{
			if (port.device == nullptr)
				continue;
			if (!negotiate(port, rate_, &port.container))
			{
				std::snprintf(message, sizeof(message), "The %s endpoint accepts no exclusive-mode format at %u Hz", port.capture ? "recording" : "playback", rate_);
				for (Port& opened : ports_)
					opened.closeDevice();
				setError(message);
				return ASIOFalse;
			}
			port.haveContainer = true;
		}
		errorMessage_[0] = '\0';
		initialized_ = true;
		return ASIOTrue;
	}

	void WasapiExclusiveTarget::getDriverName(char* name)
	{
		const Port& named = ports_[0].device != nullptr || ports_[1].device == nullptr ? ports_[0] : ports_[1];
		if (named.friendlyName.empty())
			narrowName(L"WASAPI endpoint", name, 32);
		else
			narrowName(named.friendlyName, name, 32);
	}

	long WasapiExclusiveTarget::getDriverVersion()
	{
		return 1;
	}

	void WasapiExclusiveTarget::getErrorMessage(char* string)
	{
		std::memcpy(string, errorMessage_, sizeof(errorMessage_));
	}

	// ---- queries ----

	ASIOError WasapiExclusiveTarget::getChannels(long* numInputChannels, long* numOutputChannels)
	{
		if (!initialized_)
			return ASE_NotPresent;
		if (numInputChannels != nullptr)
			*numInputChannels = ports_[1].device != nullptr ? static_cast<long>(ports_[1].channels) : 0;
		if (numOutputChannels != nullptr)
			*numOutputChannels = ports_[0].device != nullptr ? static_cast<long>(ports_[0].channels) : 0;
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::getLatencies(long* inputLatency, long* outputLatency)
	{
		if (!initialized_)
			return ASE_NotPresent;
		// Output: the period the host fills, the device period it is queued
		// behind (bridge ASIO periods), and what the driver reports. Input:
		// the device period being captured and the driver's share.
		const long bridge = static_cast<long>(bridge_.load(std::memory_order_acquire));
		if (inputLatency != nullptr)
			*inputLatency = ports_[1].device != nullptr ? bridge * frames_ + ports_[1].latencyFrames : 0;
		if (outputLatency != nullptr)
			*outputLatency = ports_[0].device != nullptr ? (bridge + 1) * frames_ + ports_[0].latencyFrames : 0;
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity)
	{
		if (!initialized_)
			return ASE_NotPresent;
		unsigned minPeriod = 0;
		for (const Port& port : ports_)
			if (port.device != nullptr && port.minPeriodFrames > minPeriod)
				minPeriod = port.minPeriodFrames;
		const wasapi::BufferPolicy policy = wasapi::bufferPolicy(minPeriod);
		if (minSize != nullptr)
			*minSize = policy.minSize;
		if (maxSize != nullptr)
			*maxSize = policy.maxSize;
		if (preferredSize != nullptr)
			*preferredSize = policy.preferredSize;
		if (granularity != nullptr)
			*granularity = policy.granularity;
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::canSampleRate(ASIOSampleRate sampleRate)
	{
		if (!initialized_)
			return ASE_NotPresent;
		if (!(sampleRate > 0.0) || sampleRate > 1000000.0)
			return ASE_NoClock;
		const unsigned rate = static_cast<unsigned>(sampleRate + 0.5);
		for (Port& port : ports_)
			if (port.device != nullptr && !negotiate(port, rate, nullptr))
				return ASE_NoClock;
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::getSampleRate(ASIOSampleRate* sampleRate)
	{
		if (!initialized_ || rate_ == 0)
			return ASE_NoClock;
		if (sampleRate != nullptr)
			*sampleRate = static_cast<ASIOSampleRate>(rate_);
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::setSampleRate(ASIOSampleRate sampleRate)
	{
		if (!initialized_)
			return ASE_NotPresent;
		if (prepared_)
			return ASE_InvalidMode;
		if (!(sampleRate > 0.0) || sampleRate > 1000000.0)
			return ASE_InvalidParameter;
		const unsigned rate = static_cast<unsigned>(sampleRate + 0.5);
		wasapi::Container found[2];
		for (size_t i = 0; i < 2; i++)
			if (ports_[i].device != nullptr && !negotiate(ports_[i], rate, &found[i]))
				return ASE_NoClock;
		for (size_t i = 0; i < 2; i++)
			if (ports_[i].device != nullptr)
				ports_[i].container = found[i];
		rate_ = rate;
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::getClockSources(ASIOClockSource* clocks, long* numSources)
	{
		if (clocks == nullptr || numSources == nullptr || *numSources < 1)
			return ASE_InvalidParameter;
		clocks[0].index = 0;
		clocks[0].associatedChannel = -1;
		clocks[0].associatedGroup = -1;
		clocks[0].isCurrentSource = ASIOTrue;
		std::snprintf(clocks[0].name, sizeof(clocks[0].name), "%s", "Internal");
		*numSources = 1;
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::setClockSource(long reference)
	{
		return reference == 0 ? ASE_OK : ASE_InvalidParameter;
	}

	ASIOError WasapiExclusiveTarget::getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp)
	{
		if (!running_)
			return ASE_SPNotAdvancing;
		if (sPos != nullptr)
			splitInt64(samplePosition_, sPos->hi, sPos->lo);
		if (tStamp != nullptr)
			splitInt64(nowNanoseconds(), tStamp->hi, tStamp->lo);
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::getChannelInfo(ASIOChannelInfo* info)
	{
		if (info == nullptr)
			return ASE_InvalidParameter;
		if (!initialized_)
			return ASE_NotPresent;
		const Port& port = info->isInput ? ports_[1] : ports_[0];
		if (port.device == nullptr || info->channel < 0 || info->channel >= static_cast<long>(port.channels))
			return ASE_InvalidParameter;
		info->type = port.container.asioType;
		info->channelGroup = 0;
		info->isActive = prepared_ ? ASIOTrue : ASIOFalse;
		std::snprintf(info->name, sizeof(info->name), "%s %ld", info->isInput ? "In" : "Out", info->channel + 1);
		return ASE_OK;
	}

	// ---- buffers ----

	HRESULT WasapiExclusiveTarget::initializeStream(Port& port, long frames, unsigned bridge)
	{
		WAVEFORMATEXTENSIBLE fmt = wasapi::makeFormat(port.container, port.channels, rate_, port.channelMask);
		const long deviceFrames = frames * static_cast<long>(bridge);
		const REFERENCE_TIME period = wasapi::hnsFromFrames(static_cast<unsigned>(deviceFrames), rate_);
		HRESULT hr = port.client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &fmt.Format, nullptr);
		if (FAILED(hr))
		{
			UINT32 aligned = 0;
			if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED && SUCCEEDED(port.client->GetBufferSize(&aligned)) && aligned != 0)
				std::snprintf(errorMessage_, sizeof(errorMessage_), "The %s endpoint needs a buffer of %u frames", port.capture ? "recording" : "playback", static_cast<unsigned>(aligned));
			else
				std::snprintf(errorMessage_, sizeof(errorMessage_), "The %s endpoint: %s (0x%08lx)", port.capture ? "recording" : "playback", describe(hr), static_cast<unsigned long>(hr));
			// The client is spent after a failed Initialize; a fresh one
			// keeps the entry usable for the next attempt.
			releaseAndNull(port.client);
			port.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&port.client));
			return hr;
		}
		UINT32 bufferFrames = 0;
		port.client->GetBufferSize(&bufferFrames);
		if (bufferFrames != static_cast<UINT32>(deviceFrames))
		{
			std::snprintf(errorMessage_, sizeof(errorMessage_), "The %s endpoint opened with %u frames, not %ld", port.capture ? "recording" : "playback", static_cast<unsigned>(bufferFrames), deviceFrames);
			port.closeStream();
			return E_FAIL;
		}
		port.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (port.event == nullptr || FAILED(hr = port.client->SetEventHandle(port.event)))
		{
			setError("The stream event could not be set");
			port.closeStream();
			return FAILED(hr) ? hr : E_FAIL;
		}
		if (port.capture)
			hr = port.client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&port.captureClient));
		else
			hr = port.client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&port.render));
		if (FAILED(hr))
		{
			std::snprintf(errorMessage_, sizeof(errorMessage_), "The %s endpoint gave no stream service (0x%08lx)", port.capture ? "recording" : "playback", static_cast<unsigned long>(hr));
			port.closeStream();
			return hr;
		}
		REFERENCE_TIME latency = 0;
		if (SUCCEEDED(port.client->GetStreamLatency(&latency)))
			port.latencyFrames = static_cast<long>(wasapi::framesFromHns(latency, rate_));

		const size_t frameBytes = static_cast<size_t>(port.channels) * port.container.bytes();
		port.block.assign(static_cast<size_t>(deviceFrames) * frameBytes, 0);
		if (port.capture)
		{
			port.pending.assign(static_cast<size_t>(deviceFrames) * frameBytes * 2, 0);
			port.pendingFrames = 0;
		}
		port.bridge = bridge;
		port.staged = 0;
		return S_OK;
	}

	bool WasapiExclusiveTarget::prepareStreams(long frames, unsigned bridge, char* message)
	{
		for (size_t i = 0; i < 2; i++)
		{
			Port& port = ports_[i];
			if (port.device == nullptr)
				continue;
			if (FAILED(initializeStream(port, frames, bridge)))
			{
				std::memcpy(message, errorMessage_, sizeof(errorMessage_));
				for (size_t j = 0; j < i; j++)
					if (ports_[j].device != nullptr)
						ports_[j].closeStream();
				return false;
			}
		}
		return true;
	}

	ASIOError WasapiExclusiveTarget::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks)
	{
		if (!initialized_)
			return ASE_NotPresent;
		if (prepared_)
			return ASE_InvalidMode;
		if (bufferInfos == nullptr || numChannels <= 0 || bufferSize <= 0 || callbacks == nullptr)
			return ASE_InvalidParameter;
		long minSize = 0, maxSize = 0, preferred = 0, granularity = 0;
		getBufferSize(&minSize, &maxSize, &preferred, &granularity);
		if (bufferSize < minSize || bufferSize > maxSize)
		{
			std::snprintf(errorMessage_, sizeof(errorMessage_), "A buffer of %ld frames is outside %ld..%ld", bufferSize, minSize, maxSize);
			return ASE_InvalidParameter;
		}
		for (long i = 0; i < numChannels; i++)
		{
			const Port& port = bufferInfos[i].isInput ? ports_[1] : ports_[0];
			if (port.device == nullptr || bufferInfos[i].channelNum < 0 || bufferInfos[i].channelNum >= static_cast<long>(port.channels))
			{
				setError("A channel that does not exist was requested");
				return ASE_InvalidParameter;
			}
		}

		char message[124] = {};
		if (!prepareStreams(bufferSize, 1, message))
		{
			setError(message);
			return ASE_HWMalfunction;
		}
		for (Port& port : ports_)
		{
			if (port.device == nullptr)
				continue;
			const size_t planeBytes = static_cast<size_t>(bufferSize) * port.container.bytes();
			for (int half = 0; half < 2; half++)
			{
				port.planes[half].assign(port.channels, std::vector<unsigned char>());
				for (unsigned c = 0; c < port.channels; c++)
					port.planes[half][c].assign(planeBytes, 0);
			}
		}
		bridge_.store(1, std::memory_order_release);
		callbacks_ = *callbacks;
		hostSupportsTimeInfo_ = callbacks_.bufferSwitchTimeInfo != nullptr && callbacks_.asioMessage != nullptr
			&& callbacks_.asioMessage(kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) == 1
			&& callbacks_.asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
		frames_ = bufferSize;
		for (long i = 0; i < numChannels; i++)
		{
			Port& port = bufferInfos[i].isInput ? ports_[1] : ports_[0];
			bufferInfos[i].buffers[0] = port.planes[0][static_cast<size_t>(bufferInfos[i].channelNum)].data();
			bufferInfos[i].buffers[1] = port.planes[1][static_cast<size_t>(bufferInfos[i].channelNum)].data();
		}
		samplePosition_ = 0;
		counters_ = Counters();
		prepared_ = true;
		errorMessage_[0] = '\0';
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::disposeBuffers()
	{
		if (!prepared_)
			return ASE_InvalidMode;
		stop();
		for (Port& port : ports_)
		{
			if (port.device == nullptr)
				continue;
			port.closeStream();
			port.releasePlanes();
		}
		prepared_ = false;
		frames_ = 0;
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::controlPanel()
	{
		return ASE_NotPresent;
	}

	ASIOError WasapiExclusiveTarget::future(long selector, void* /*opt*/)
	{
		switch (selector)
		{
		case kAsioCanTimeInfo:
			return ASE_SUCCESS;
		default:
			return ASE_NotPresent;
		}
	}

	ASIOError WasapiExclusiveTarget::outputReady()
	{
		// Called from inside the buffer switch on the stream thread: the
		// host's output is final, hand it to the device now rather than
		// after the callback returns.
		const long half = pendingHalf_.load(std::memory_order_acquire);
		if (half >= 0 && !committed_.load(std::memory_order_acquire) && GetCurrentThreadId() == threadId_.load(std::memory_order_acquire))
			commitOutput(half);
		return ASE_OK;
	}

	// ---- streaming ----

	ASIOError WasapiExclusiveTarget::start()
	{
		if (!prepared_)
			return ASE_InvalidMode;
		if (running_)
			return ASE_OK;
		stopRequested_ = false;
		running_ = true;
		try
		{
			thread_ = std::thread(&WasapiExclusiveTarget::streamThread, this);
		}
		catch (...)
		{
			running_ = false;
			setError("The stream thread could not be started");
			return ASE_HWMalfunction;
		}
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::stop()
	{
		if (!running_)
			return ASE_OK;
		stopRequested_ = true;
		if (thread_.joinable())
			thread_.join();
		running_ = false;
		return ASE_OK;
	}

	void WasapiExclusiveTarget::fillTimeInfo(ASIOTime& time) const noexcept
	{
		splitInt64(samplePosition_, time.timeInfo.samplePosition.hi, time.timeInfo.samplePosition.lo);
		splitInt64(nowNanoseconds(), time.timeInfo.systemTime.hi, time.timeInfo.systemTime.lo);
		time.timeInfo.sampleRate = static_cast<ASIOSampleRate>(rate_);
		time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
	}

	void WasapiExclusiveTarget::drainCapture(Port& port) noexcept
	{
		if (port.captureClient == nullptr)
			return;
		const size_t frameBytes = static_cast<size_t>(port.channels) * port.container.bytes();
		const size_t capacityFrames = port.pending.size() / frameBytes;
		UINT32 packet = 0;
		while (SUCCEEDED(port.captureClient->GetNextPacketSize(&packet)) && packet > 0)
		{
			BYTE* data = nullptr;
			UINT32 frames = 0;
			DWORD flags = 0;
			if (FAILED(port.captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
				break;
			if (frames > capacityFrames)
				frames = static_cast<UINT32>(capacityFrames);
			// Bounded at two periods: older audio makes way so the input
			// never drifts further than that behind the output clock.
			if (port.pendingFrames + frames > capacityFrames)
			{
				const size_t drop = port.pendingFrames + frames - capacityFrames;
				std::memmove(port.pending.data(), port.pending.data() + drop * frameBytes, (port.pendingFrames - drop) * frameBytes);
				port.pendingFrames -= drop;
			}
			unsigned char* at = port.pending.data() + port.pendingFrames * frameBytes;
			if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
				std::memset(at, 0, static_cast<size_t>(frames) * frameBytes);
			else
				std::memcpy(at, data, static_cast<size_t>(frames) * frameBytes);
			port.pendingFrames += frames;
			port.captureClient->ReleaseBuffer(frames);
		}
	}

	void WasapiExclusiveTarget::commitOutput(long half) noexcept
	{
		Port& port = ports_[0];
		if (port.render == nullptr)
		{
			committed_.store(true, std::memory_order_release);
			return;
		}
		// The ASIO period goes into the device block at its slot; the block
		// is handed to the device once every ASIO period of the device
		// period is in it (one write per event with bridge 1).
		const size_t frameBytes = static_cast<size_t>(port.channels) * port.container.bytes();
		std::vector<const void*> planes(port.channels);
		for (unsigned c = 0; c < port.channels; c++)
			planes[c] = port.planes[half][c].data();
		wasapi::interleave(planes.data(), port.channels, port.container.bytes(), static_cast<unsigned>(frames_),
			port.block.data() + static_cast<size_t>(port.staged) * static_cast<size_t>(frames_) * frameBytes);
		port.staged++;
		committed_.store(true, std::memory_order_release);
		if (port.staged < port.bridge)
			return;
		port.staged = 0;
		const UINT32 deviceFrames = static_cast<UINT32>(frames_) * port.bridge;
		BYTE* data = nullptr;
		if (FAILED(port.render->GetBuffer(deviceFrames, &data)))
		{
			counters_.outputMisses++;
			return;
		}
		std::memcpy(data, port.block.data(), static_cast<size_t>(deviceFrames) * frameBytes);
		port.render->ReleaseBuffer(deviceFrames, 0);
	}

	void WasapiExclusiveTarget::servePeriod(long half) noexcept
	{
		Port& in = ports_[1];
		if (in.captureClient != nullptr)
		{
			drainCapture(in);
			const size_t frameBytes = static_cast<size_t>(in.channels) * in.container.bytes();
			std::vector<void*> planes(in.channels);
			for (unsigned c = 0; c < in.channels; c++)
				planes[c] = in.planes[half][c].data();
			if (in.pendingFrames >= static_cast<size_t>(frames_))
			{
				wasapi::deinterleave(in.pending.data(), in.channels, in.container.bytes(), static_cast<unsigned>(frames_), planes.data());
				in.pendingFrames -= static_cast<size_t>(frames_);
				std::memmove(in.pending.data(), in.pending.data() + static_cast<size_t>(frames_) * frameBytes, in.pendingFrames * frameBytes);
			}
			else
			{
				for (unsigned c = 0; c < in.channels; c++)
					std::memset(planes[c], 0, in.planes[half][c].size());
				counters_.inputUnderruns++;
			}
		}

		pendingHalf_.store(half, std::memory_order_release);
		committed_.store(false, std::memory_order_release);
		if (hostSupportsTimeInfo_)
		{
			ASIOTime time = {};
			fillTimeInfo(time);
			callbacks_.bufferSwitchTimeInfo(&time, half, ASIOTrue);
		}
		else if (callbacks_.bufferSwitch != nullptr)
		{
			callbacks_.bufferSwitch(half, ASIOTrue);
		}
		if (!committed_.load(std::memory_order_acquire))
			commitOutput(half);
		pendingHalf_.store(-1, std::memory_order_release);
		samplePosition_ += static_cast<uint64_t>(frames_);
		counters_.periods++;
	}

	// One silent device period ahead of the host's first real one, so the
	// device has something to play from the first event.
	void WasapiExclusiveTarget::primeOutput() noexcept
	{
		Port& out = ports_[0];
		if (out.render == nullptr)
			return;
		const UINT32 deviceFrames = static_cast<UINT32>(frames_) * out.bridge;
		BYTE* data = nullptr;
		if (SUCCEEDED(out.render->GetBuffer(deviceFrames, &data)))
			out.render->ReleaseBuffer(deviceFrames, AUDCLNT_BUFFERFLAGS_SILENT);
	}

	// Reopens both streams with a device period of `factor` ASIO periods.
	// Stream thread only, between events. The ASIO buffers stay where they
	// are; only the device side changes, and the host hears about the new
	// latency through kAsioLatenciesChanged when it supports the message.
	bool WasapiExclusiveTarget::rebridge(unsigned factor) noexcept
	{
		Port& out = ports_[0];
		Port& in = ports_[1];
		if (out.client != nullptr)
			out.client->Stop();
		if (in.client != nullptr)
			in.client->Stop();
		for (Port& port : ports_)
			if (port.device != nullptr)
				port.closeStream();
		char message[124] = {};
		if (!prepareStreams(frames_, factor, message))
		{
			setError(message);
			return false;
		}
		primeOutput();
		if (in.client != nullptr && in.captureClient != nullptr && FAILED(in.client->Start()))
			return false;
		if (out.client != nullptr && out.render != nullptr && FAILED(out.client->Start()))
			return false;
		bridge_.store(factor, std::memory_order_release);
		counters_.bridge = factor;
		if (callbacks_.asioMessage != nullptr
			&& callbacks_.asioMessage(kAsioSelectorSupported, kAsioLatenciesChanged, nullptr, nullptr) == 1)
			callbacks_.asioMessage(kAsioLatenciesChanged, 0, nullptr, nullptr);
		return true;
	}

	void WasapiExclusiveTarget::streamThread() noexcept
	{
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		threadId_.store(GetCurrentThreadId(), std::memory_order_release);
		// A driver's period is only as fine as the system timer it runs on.
		// With the default 15.6 ms resolution a virtual cable accepted a
		// 5.8 ms period and signalled every 15.9 ms, leaving two thirds of
		// every period unplayed. DAWs and ASIO drivers ask for 1 ms while
		// they stream; so does this target, for the life of the stream.
		const MMRESULT timerRequest = timeBeginPeriod(1);
		DWORD taskIndex = 0;
		HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

		Port& out = ports_[0];
		Port& in = ports_[1];
		primeOutput();
		bool started = true;
		if (in.client != nullptr && in.captureClient != nullptr && FAILED(in.client->Start()))
			started = false;
		if (started && out.client != nullptr && out.render != nullptr && FAILED(out.client->Start()))
			started = false;

		// Some drivers accept a small period and then signal at their own
		// coarser cycle (a virtual cable: every 10 ms against a 5.8 ms
		// period), consuming a whole cycle's worth per event; one ASIO
		// period per event then leaves the rest unplayed. The first events
		// tell: when their typical spacing is well over the period, the
		// device side is reopened at the smallest multiple of the ASIO
		// period that covers the cycle, and every event serves that many
		// ASIO periods back to back. The host keeps its buffer size; the
		// stream keeps its audio; only the latency grows, and is reported.
		// EAPO_WASAPI_FORCE_BRIDGE=<n> takes that decision up front, for
		// exercising the path on a driver that does not need it.
		constexpr unsigned calibrationEvents = 12;
		constexpr unsigned bridgeCap = 8;
		unsigned forcedBridge = 0;
		{
			wchar_t value[8] = {};
			if (GetEnvironmentVariableW(L"EAPO_WASAPI_FORCE_BRIDGE", value, 8) > 0)
			{
				const int parsed = _wtoi(value);
				if (parsed >= 2 && parsed <= static_cast<int>(bridgeCap))
					forcedBridge = static_cast<unsigned>(parsed);
			}
		}
		if (started && forcedBridge != 0 && !rebridge(forcedBridge))
			started = false;
		bool calibrated = forcedBridge != 0;
		uint64_t calibration[calibrationEvents] = {};
		unsigned calibrationCount = 0;

		HANDLE clock = out.event != nullptr ? out.event : in.event;
		long half = 0;
		uint64_t periodNanos = rate_ != 0 ? static_cast<uint64_t>(static_cast<double>(frames_) * 1e9 / static_cast<double>(rate_)) : 0;
		uint64_t devicePeriodNanos = periodNanos * bridge_.load(std::memory_order_acquire);
		uint64_t previousEvent = 0;
		uint64_t intervalSum = 0, intervalCount = 0, intervalMax = 0, serviceMax = 0;
		while (started && !stopRequested_.load(std::memory_order_acquire))
		{
			const DWORD waited = WaitForSingleObject(clock, 500);
			if (waited == WAIT_TIMEOUT)
				continue;
			if (waited != WAIT_OBJECT_0)
				break;
			const uint64_t now = nowNanoseconds();
			if (previousEvent != 0)
			{
				const uint64_t interval = now - previousEvent;
				if (devicePeriodNanos != 0 && interval > devicePeriodNanos + devicePeriodNanos * 3 / 4)
					counters_.slowEvents++;
				intervalSum += interval;
				intervalCount++;
				if (interval > intervalMax)
					intervalMax = interval;
				if (!calibrated && calibrationCount < calibrationEvents)
					calibration[calibrationCount++] = interval;
			}
			previousEvent = now;
			const unsigned bridge = bridge_.load(std::memory_order_acquire);
			for (unsigned k = 0; k < bridge; k++)
			{
				servePeriod(half);
				half ^= 1;
			}
			const uint64_t served = nowNanoseconds() - now;
			if (served > serviceMax)
				serviceMax = served;

			if (!calibrated && calibrationCount == calibrationEvents && periodNanos != 0)
			{
				calibrated = true;
				// The median spacing, so a stray stall does not decide.
				uint64_t sorted[calibrationEvents];
				std::memcpy(sorted, calibration, sizeof(sorted));
				std::sort(sorted, sorted + calibrationEvents);
				const uint64_t typical = sorted[calibrationEvents / 2];
				if (typical > periodNanos + periodNanos / 2)
				{
					unsigned factor = static_cast<unsigned>((typical + periodNanos - 1) / periodNanos);
					if (factor > bridgeCap)
						factor = bridgeCap;
					if (factor >= 2)
					{
						// A device that will not reopen ends the stream, as a
						// device that vanished would; the host sees no more switches.
						if (!rebridge(factor))
							break;
						clock = out.event != nullptr ? out.event : in.event;
						devicePeriodNanos = periodNanos * factor;
						previousEvent = 0;
					}
				}
			}
		}
		counters_.eventIntervalAvgUs = intervalCount != 0 ? intervalSum / intervalCount / 1000 : 0;
		counters_.eventIntervalMaxUs = intervalMax / 1000;
		counters_.serviceMaxUs = serviceMax / 1000;
		counters_.bridge = bridge_.load(std::memory_order_acquire);

		if (out.client != nullptr)
			out.client->Stop();
		if (in.client != nullptr)
			in.client->Stop();
		if (task != nullptr)
			AvRevertMmThreadCharacteristics(task);
		if (timerRequest == TIMERR_NOERROR)
			timeEndPeriod(1);
		threadId_.store(0, std::memory_order_release);
		if (SUCCEEDED(com))
			CoUninitialize();
	}
}
