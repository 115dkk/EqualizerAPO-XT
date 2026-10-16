/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/WasapiExclusiveTarget.h"

#include "asio/Clock.h"
#include "asio/DriverNameText.h"
#include "asio/HostCallbacks.h"
#include "asio/WaveFormat.h"

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
				waveformat::Container described;
				if (!waveformat::containerOf(format, described))
					return false;
				if (described.floating)
				{
					if (described.bits != 32 || described.validBits != 32)
						return false;
					out = float32Container;
					return true;
				}
				switch (described.bits)
				{
				case 32:
					out = described.validBits >= 32 ? int32Container : int24In32Container;
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
			const unsigned smallest = size / bridgeCap;
			policy.minSize = static_cast<long>(smallest > 32 ? smallest : 32);
			policy.maxSize = static_cast<long>(size > 2048 ? size : 2048);
			policy.preferredSize = static_cast<long>(size);
			policy.granularity = -1;
			return policy;
		}

		unsigned initialBridge(long frames, long preferredSize) noexcept
		{
			if (frames <= 0 || frames >= preferredSize)
				return 1;
			const long count = (preferredSize + frames - 1) / frames;
			return static_cast<unsigned>(count < static_cast<long>(bridgeCap) ? count : bridgeCap);
		}

		CapturePlan planCapturePacket(size_t pendingFrames, size_t capacityFrames, size_t packetFrames) noexcept
		{
			CapturePlan plan;
			plan.copyFrames = std::min(packetFrames, capacityFrames);
			if (pendingFrames + plan.copyFrames > capacityFrames)
				plan.dropFromQueue = pendingFrames + plan.copyFrames - capacityFrames;
			return plan;
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

		// ---- BridgeCalibrator ----

		BridgeCalibrator::BridgeCalibrator(uint64_t periodNanos, unsigned currentBridge, int forcedBridge) noexcept
			: periodNanos_(periodNanos), current_(currentBridge != 0 ? currentBridge : 1), factor_(current_)
		{
			if (forcedBridge > static_cast<int>(current_) && forcedBridge <= static_cast<int>(bridgeCap))
			{
				factor_ = static_cast<unsigned>(forcedBridge);
				forced_ = true;
				decided_ = true;
			}
		}

		bool BridgeCalibrator::addSpacing(uint64_t spacingNanos) noexcept
		{
			if (decided_)
				return false;
			spacings_[count_++] = spacingNanos;
			if (count_ < bridgeCalibrationEvents)
				return false;
			decided_ = true;
			if (periodNanos_ == 0)
				return true;
			// The median spacing, so a stray stall does not decide.
			uint64_t sorted[bridgeCalibrationEvents];
			std::memcpy(sorted, spacings_, sizeof(sorted));
			std::sort(sorted, sorted + bridgeCalibrationEvents);
			const uint64_t typical = sorted[bridgeCalibrationEvents / 2];
			const uint64_t devicePeriod = periodNanos_ * current_;
			const uint64_t threshold = devicePeriod + devicePeriod * (bridgeThresholdNumerator - bridgeThresholdDenominator) / bridgeThresholdDenominator;
			if (typical > threshold)
			{
				unsigned factor = static_cast<unsigned>((typical + periodNanos_ - 1) / periodNanos_);
				if (factor > bridgeCap)
					factor = bridgeCap;
				if (factor > current_)
					factor_ = factor;
			}
			return true;
		}

		// ---- CaptureQueue ----

		void CaptureQueue::reset(unsigned channels, unsigned bytesPerSample, size_t capacityFrames)
		{
			channels_ = channels;
			bytesPerSample_ = bytesPerSample;
			frameBytes_ = static_cast<size_t>(channels) * bytesPerSample;
			storage_.assign(capacityFrames * frameBytes_, 0);
			pendingFrames_ = 0;
		}

		void CaptureQueue::clear() noexcept
		{
			storage_.clear();
			pendingFrames_ = 0;
		}

		void CaptureQueue::push(const void* data, size_t frames, bool silent) noexcept
		{
			const CapturePlan plan = planCapturePacket(pendingFrames_, capacityFrames(), frames);
			if (plan.dropFromQueue != 0)
			{
				std::memmove(storage_.data(), storage_.data() + plan.dropFromQueue * frameBytes_,
					(pendingFrames_ - plan.dropFromQueue) * frameBytes_);
				pendingFrames_ -= plan.dropFromQueue;
			}
			if (plan.copyFrames == 0)
				return;
			unsigned char* at = storage_.data() + pendingFrames_ * frameBytes_;
			if (silent || data == nullptr)
				std::memset(at, 0, plan.copyFrames * frameBytes_);
			else
				std::memcpy(at, data, plan.copyFrames * frameBytes_);
			pendingFrames_ += plan.copyFrames;
		}

		bool CaptureQueue::take(void* const* planes, unsigned frames) noexcept
		{
			if (pendingFrames_ >= static_cast<size_t>(frames) && frameBytes_ != 0)
			{
				deinterleave(storage_.data(), channels_, bytesPerSample_, frames, planes);
				pendingFrames_ -= static_cast<size_t>(frames);
				std::memmove(storage_.data(), storage_.data() + static_cast<size_t>(frames) * frameBytes_, pendingFrames_ * frameBytes_);
				return true;
			}
			for (unsigned c = 0; c < channels_; c++)
				std::memset(planes[c], 0, static_cast<size_t>(frames) * bytesPerSample_);
			underruns_++;
			return false;
		}

		// ---- OutputStager ----

		void OutputStager::reset(unsigned bridge, unsigned framesPerPeriod) noexcept
		{
			bridge_ = bridge != 0 ? bridge : 1;
			framesPerPeriod_ = framesPerPeriod;
			staged_ = 0;
		}

		OutputStager::Step OutputStager::stage() noexcept
		{
			Step step;
			step.slot = staged_;
			staged_++;
			if (staged_ < bridge_)
				return step;
			staged_ = 0;
			step.writeFrames = framesPerPeriod_ * bridge_;
			return step;
		}

		// ---- StreamHealthJudge ----

		StreamHealth StreamHealthJudge::judgeWait(bool timedOut, HRESULT deviceResult) noexcept
		{
			if (deviceResult == AUDCLNT_E_DEVICE_INVALIDATED)
				return StreamHealth::DeviceLost;
			if (!timedOut)
			{
				timeouts_ = 0;
				return StreamHealth::Continue;
			}
			if (timeouts_ < deviceLostTimeouts)
				timeouts_++;
			return timeouts_ >= deviceLostTimeouts ? StreamHealth::DeviceLost : StreamHealth::Retry;
		}
	}

	namespace
	{
		// PKEY_AudioEngine_DeviceFormat ({F19F064D-082C-4E27-BC73-6882A1BB8E4C},0):
		// the format the Sound settings show for the endpoint. Spelled out
		// because the SDK only defines the key for INITGUID translation units.
		const PROPERTYKEY keyDeviceFormat = {{0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}}, 0};

		void splitInt64(uint64_t value, unsigned long& hi, unsigned long& lo) noexcept
		{
			hi = static_cast<unsigned long>(value >> 32);
			lo = static_cast<unsigned long>(value & 0xffffffffu);
		}

		// The endpoint's name in the 32 bytes ASIO gives an ANSI driver name.
		void narrowName(const std::wstring& wide, char* out, size_t capacity) noexcept
		{
			if (capacity == 0)
				return;
			char converted[256] = {};
			BOOL usedDefault = FALSE;
			const int written = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), -1,
				converted, static_cast<int>(sizeof(converted)), "?", &usedDefault);
			if (written > 0)
			{
				const size_t length = std::strlen(converted);
				const size_t copy = drivername::characterBoundary(converted, length, capacity - 1, CP_ACP);
				std::memcpy(out, converted, copy);
				out[copy] = '\0';
				return;
			}

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

		class RealEndpointClient final : public wasapi::IEndpointClient
		{
		public:
			RealEndpointClient(IMMDevice* device, IAudioClient* client, bool capture)
				: capture_(capture)
			{
				device->AddRef();
				*device_.put() = device;
				client->AddRef();
				*client_.put() = client;
			}

			HRESULT isFormatSupported(const WAVEFORMATEX* format) noexcept override
			{
				return client_ != nullptr ? client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, format, nullptr) : E_FAIL;
			}

			HRESULT initialize(const WAVEFORMATEX* format, REFERENCE_TIME period, UINT32 /*expectedFrames*/,
				UINT32& actualFrames, REFERENCE_TIME& latency) noexcept override
			{
				actualFrames = 0;
				latency = 0;
				if (client_ == nullptr)
				{
					const HRESULT activated = renew();
					if (FAILED(activated) || client_ == nullptr)
						return FAILED(activated) ? activated : E_FAIL;
				}
				HRESULT hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
					period, period, format, nullptr);
				if (FAILED(hr))
				{
					client_->GetBufferSize(&actualFrames);
					renew();
					return hr;
				}
				hr = client_->GetBufferSize(&actualFrames);
				if (FAILED(hr))
					return hr;
				event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
				if (!event_)
					return HRESULT_FROM_WIN32(GetLastError());
				hr = client_->SetEventHandle(event_.get());
				if (FAILED(hr))
					return hr;
				if (capture_)
					hr = client_->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(captureClient_.put()));
				else
					hr = client_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render_.put()));
				if (FAILED(hr))
					return hr;
				client_->GetStreamLatency(&latency);
				return S_OK;
			}

			void close() noexcept override
			{
				if (client_ != nullptr)
					client_->Stop();
				render_.reset();
				captureClient_.reset();
				event_.reset();
				renew();
			}

			HRESULT start() noexcept override {return client_ != nullptr ? client_->Start() : E_FAIL;}
			HRESULT stop() noexcept override {return client_ != nullptr ? client_->Stop() : S_OK;}
			HANDLE event() const noexcept override {return event_.get();}
			HRESULT getRenderBuffer(UINT32 frames, BYTE** data) noexcept override
			{
				return render_ != nullptr ? render_->GetBuffer(frames, data) : E_NOINTERFACE;
			}
			HRESULT releaseRenderBuffer(UINT32 frames, DWORD flags) noexcept override
			{
				return render_ != nullptr ? render_->ReleaseBuffer(frames, flags) : E_NOINTERFACE;
			}
			HRESULT getNextPacketSize(UINT32* frames) noexcept override
			{
				return captureClient_ != nullptr ? captureClient_->GetNextPacketSize(frames) : E_NOINTERFACE;
			}
			HRESULT getCaptureBuffer(BYTE** data, UINT32* frames, DWORD* flags) noexcept override
			{
				return captureClient_ != nullptr ? captureClient_->GetBuffer(data, frames, flags, nullptr, nullptr) : E_NOINTERFACE;
			}
			HRESULT releaseCaptureBuffer(UINT32 frames) noexcept override
			{
				return captureClient_ != nullptr ? captureClient_->ReleaseBuffer(frames) : E_NOINTERFACE;
			}

		private:
			HRESULT renew() noexcept
			{
				client_.reset();
				return device_ != nullptr
					? device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client_.put()))
					: E_FAIL;
			}

			bool capture_;
			winutil::ComPtr<IMMDevice> device_;
			winutil::ComPtr<IAudioClient> client_;
			winutil::ComPtr<IAudioRenderClient> render_;
			winutil::ComPtr<IAudioCaptureClient> captureClient_;
			winutil::UniqueHandle event_;
		};
	}

	// ---- Port ----

	void WasapiExclusiveTarget::Port::closeStream() noexcept
	{
		if (endpoint != nullptr)
			endpoint->close();
		block.clear();
		queue.clear();
		stager.reset(bridge, 0);
		latencyFrames.store(0, std::memory_order_relaxed);
	}

	// The ASIO buffers outlive a stream: the host holds their pointers from
	// createBuffers to disposeBuffers, across a rebridge that reopens the
	// device with another period.
	void WasapiExclusiveTarget::Port::releasePlanes() noexcept
	{
		planes[0].clear();
		planes[1].clear();
		planePointers[0].clear();
		planePointers[1].clear();
	}

	void WasapiExclusiveTarget::Port::closeDevice() noexcept
	{
		closeStream();
		releasePlanes();
		endpoint.reset();
		device.reset();
	}

	// ---- lifetime ----

	WasapiExclusiveTarget::WasapiExclusiveTarget(std::wstring renderEndpointGuid, std::wstring captureEndpointGuid)
	{
		ports_[0].capture = false;
		ports_[0].endpointGuid = std::move(renderEndpointGuid);
		ports_[1].capture = true;
		ports_[1].endpointGuid = std::move(captureEndpointGuid);
		stopEvent_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		if (!stopEvent_)
		{
			setError("The stream stop event could not be created");
			return;
		}
		startAckEvent_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
		if (!startAckEvent_)
			setError("The stream start event could not be created");
	}

	WasapiExclusiveTarget::WasapiExclusiveTarget(wasapi::EndpointSetup render, wasapi::EndpointSetup capture)
	{
		injectedEndpoints_ = true;
		ports_[0].capture = false;
		ports_[1].capture = true;
		wasapi::EndpointSetup setups[2] = {std::move(render), std::move(capture)};
		for (size_t i = 0; i < 2; i++)
		{
			Port& port = ports_[i];
			wasapi::EndpointSetup& setup = setups[i];
			port.endpoint = std::move(setup.client);
			port.friendlyName = std::move(setup.friendlyName);
			port.channels = setup.channels;
			port.channelMask = setup.channelMask;
			port.deviceRate = setup.sampleRate;
			port.minPeriodHns = setup.minPeriodHns;
			port.container = setup.container;
			port.haveContainer = port.endpoint != nullptr;
		}
		stopEvent_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		startAckEvent_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
		if (!stopEvent_ || !startAckEvent_)
			setError("The stream synchronization events could not be created");
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

	void WasapiExclusiveTarget::setError(const char* message) noexcept
	{
		copyErrorMessage(errorMessage_, message);
	}

	WasapiExclusiveTarget::Counters WasapiExclusiveTarget::counters() const noexcept
	{
		return counters_;
	}

	// ---- init ----

	bool WasapiExclusiveTarget::openPort(Port& port, IMMDeviceEnumerator* enumerator, char* message)
	{
		const std::wstring id = wasapi::endpointId(port.capture, port.endpointGuid);
		HRESULT hr = enumerator->GetDevice(id.c_str(), port.device.put());
		if (FAILED(hr) || port.device == nullptr)
		{
			std::snprintf(message, errorMessageBytes, "The %s endpoint is not present", port.capture ? "recording" : "playback");
			return false;
		}
		DWORD state = 0;
		if (SUCCEEDED(port.device->GetState(&state)) && state != DEVICE_STATE_ACTIVE)
		{
			std::snprintf(message, errorMessageBytes, "The %s endpoint is not active", port.capture ? "recording" : "playback");
			return false;
		}

		winutil::ComPtr<IPropertyStore> store;
		if (SUCCEEDED(port.device->OpenPropertyStore(STGM_READ, store.put())) && store != nullptr)
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
		}

		winutil::ComPtr<IAudioClient> client;
		hr = port.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.put()));
		if (FAILED(hr) || client == nullptr)
		{
			std::snprintf(message, errorMessageBytes, "The %s endpoint gave no audio client (0x%08lx)", port.capture ? "recording" : "playback", static_cast<unsigned long>(hr));
			return false;
		}
		if (port.deviceFormat.empty())
		{
			WAVEFORMATEX* mix = nullptr;
			if (SUCCEEDED(client->GetMixFormat(&mix)) && mix != nullptr)
			{
				const unsigned char* bytes = reinterpret_cast<const unsigned char*>(mix);
				port.deviceFormat.assign(bytes, bytes + sizeof(WAVEFORMATEX) + mix->cbSize);
				CoTaskMemFree(mix);
			}
		}
		if (port.deviceFormat.empty())
		{
			std::snprintf(message, errorMessageBytes, "The %s endpoint reports no format", port.capture ? "recording" : "playback");
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
			std::snprintf(message, errorMessageBytes, "The %s endpoint reports no channels or rate", port.capture ? "recording" : "playback");
			return false;
		}

		REFERENCE_TIME defaultPeriod = 0;
		client->GetDevicePeriod(&defaultPeriod, &port.minPeriodHns);
		port.endpoint = std::make_unique<RealEndpointClient>(port.device.get(), client.get(), port.capture);
		return true;
	}

	bool WasapiExclusiveTarget::negotiate(Port& port, unsigned rate, wasapi::Container* found) const
	{
		if (port.endpoint == nullptr)
			return false;
		const WAVEFORMATEX* deviceFormat = port.deviceFormat.empty() ? nullptr : reinterpret_cast<const WAVEFORMATEX*>(port.deviceFormat.data());
		for (const wasapi::Container& candidate : wasapi::containerCandidates(deviceFormat))
		{
			WAVEFORMATEXTENSIBLE fmt = wasapi::makeFormat(candidate, port.channels, rate, port.channelMask);
			if (port.endpoint->isFormatSupported(&fmt.Format) == S_OK)
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
		if (!stopEvent_ || !startAckEvent_)
			return ASIOFalse;
		if (injectedEndpoints_)
		{
			const Port& first = ports_[0].endpoint != nullptr ? ports_[0] : ports_[1];
			if (first.endpoint == nullptr || first.channels == 0 || first.deviceRate == 0)
			{
				setError("The injected endpoint reports no channels or rate");
				return ASIOFalse;
			}
			rate_ = first.deviceRate;
			for (const Port& port : ports_)
			{
				if (port.endpoint != nullptr && (port.channels == 0 || port.deviceRate != rate_ || !port.haveContainer))
				{
					setError("The injected endpoints do not share a usable stream format");
					return ASIOFalse;
				}
			}
			errorMessage_[0] = '\0';
			initialized_ = true;
			return ASIOTrue;
		}
		if (ports_[0].endpointGuid.empty() && ports_[1].endpointGuid.empty())
		{
			setError("No endpoint is recorded for this entry");
			return ASIOFalse;
		}
		winutil::ComPtr<IMMDeviceEnumerator> enumerator;
		HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
		if (FAILED(hr) || enumerator == nullptr)
		{
			setError("The audio device enumerator is not available");
			return ASIOFalse;
		}
		char message[errorMessageBytes] = {};
		for (Port& port : ports_)
		{
			if (port.endpointGuid.empty())
				continue;
			if (!openPort(port, enumerator, message))
			{
				for (Port& opened : ports_)
					opened.closeDevice();
				setError(message);
				return ASIOFalse;
			}
		}

		rate_ = ports_[0].endpoint != nullptr ? ports_[0].deviceRate : ports_[1].deviceRate;
		for (Port& port : ports_)
		{
			if (port.endpoint == nullptr)
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
		const Port& named = ports_[0].endpoint != nullptr || ports_[1].endpoint == nullptr ? ports_[0] : ports_[1];
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
			*numInputChannels = ports_[1].endpoint != nullptr ? static_cast<long>(ports_[1].channels) : 0;
		if (numOutputChannels != nullptr)
			*numOutputChannels = ports_[0].endpoint != nullptr ? static_cast<long>(ports_[0].channels) : 0;
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
			*inputLatency = ports_[1].endpoint != nullptr ? bridge * frames_ + ports_[1].latencyFrames.load(std::memory_order_relaxed) : 0;
		if (outputLatency != nullptr)
			*outputLatency = ports_[0].endpoint != nullptr ? (bridge + 1) * frames_ + ports_[0].latencyFrames.load(std::memory_order_relaxed) : 0;
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity)
	{
		if (!initialized_)
			return ASE_NotPresent;
		unsigned minPeriod = 0;
		for (const Port& port : ports_)
		{
			if (port.endpoint == nullptr)
				continue;
			unsigned frames = wasapi::framesFromHns(port.minPeriodHns, rate_);
			if (frames == 0)
				frames = 128;
			if (frames > minPeriod)
				minPeriod = frames;
		}
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
			if (port.endpoint != nullptr && !negotiate(port, rate, nullptr))
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
			if (ports_[i].endpoint != nullptr && !negotiate(ports_[i], rate, &found[i]))
				return ASE_NoClock;
		for (size_t i = 0; i < 2; i++)
			if (ports_[i].endpoint != nullptr)
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
		if (!running_.load(std::memory_order_acquire) || !threadAlive_.load(std::memory_order_acquire))
			return ASE_SPNotAdvancing;
		if (sPos != nullptr)
			splitInt64(samplePosition_.load(std::memory_order_relaxed), sPos->hi, sPos->lo);
		if (tStamp != nullptr)
			splitInt64(Clock::nanoseconds(), tStamp->hi, tStamp->lo);
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::getChannelInfo(ASIOChannelInfo* info)
	{
		if (info == nullptr)
			return ASE_InvalidParameter;
		if (!initialized_)
			return ASE_NotPresent;
		const Port& port = info->isInput ? ports_[1] : ports_[0];
		if (port.endpoint == nullptr || info->channel < 0 || info->channel >= static_cast<long>(port.channels))
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
		if (port.endpoint == nullptr)
			return E_FAIL;
		WAVEFORMATEXTENSIBLE fmt = wasapi::makeFormat(port.container, port.channels, rate_, port.channelMask);
		const long deviceFrames = frames * static_cast<long>(bridge);
		const REFERENCE_TIME period = wasapi::hnsFromFrames(static_cast<unsigned>(deviceFrames), rate_);
		UINT32 bufferFrames = 0;
		REFERENCE_TIME latency = 0;
		const HRESULT hr = port.endpoint->initialize(&fmt.Format, period, static_cast<UINT32>(deviceFrames), bufferFrames, latency);
		if (FAILED(hr))
		{
			if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED && bufferFrames != 0)
				std::snprintf(errorMessage_, sizeof(errorMessage_), "The %s endpoint needs a buffer of %u frames", port.capture ? "recording" : "playback", static_cast<unsigned>(bufferFrames));
			else
				std::snprintf(errorMessage_, sizeof(errorMessage_), "The %s endpoint: %s (0x%08lx)", port.capture ? "recording" : "playback", describe(hr), static_cast<unsigned long>(hr));
			port.closeStream();
			return hr;
		}
		if (bufferFrames != static_cast<UINT32>(deviceFrames))
		{
			std::snprintf(errorMessage_, sizeof(errorMessage_), "The %s endpoint opened with %u frames, not %ld", port.capture ? "recording" : "playback", static_cast<unsigned>(bufferFrames), deviceFrames);
			port.closeStream();
			return E_FAIL;
		}
		port.latencyFrames.store(static_cast<long>(wasapi::framesFromHns(latency, rate_)), std::memory_order_relaxed);

		const size_t frameBytes = static_cast<size_t>(port.channels) * port.container.bytes();
		port.block.assign(static_cast<size_t>(deviceFrames) * frameBytes, 0);
		if (port.capture)
			port.queue.reset(port.channels, port.container.bytes(), static_cast<size_t>(deviceFrames) * 2);
		port.bridge = bridge;
		port.stager.reset(bridge, static_cast<unsigned>(frames));
		return S_OK;
	}

	bool WasapiExclusiveTarget::prepareStreams(long frames, unsigned bridge, char* message)
	{
		for (size_t i = 0; i < 2; i++)
		{
			Port& port = ports_[i];
			if (port.endpoint == nullptr)
				continue;
			if (FAILED(initializeStream(port, frames, bridge)))
			{
				std::memcpy(message, errorMessage_, sizeof(errorMessage_));
				for (size_t j = 0; j < i; j++)
					if (ports_[j].endpoint != nullptr)
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
			if (port.endpoint == nullptr || bufferInfos[i].channelNum < 0 || bufferInfos[i].channelNum >= static_cast<long>(port.channels))
			{
				setError("A channel that does not exist was requested");
				return ASE_InvalidParameter;
			}
		}

		// A buffer below the preferred size runs the device at the preferred
		// size, each device period serving `bridge` buffers.
		const unsigned bridge = wasapi::initialBridge(bufferSize, preferred);
		char message[errorMessageBytes] = {};
		if (!prepareStreams(bufferSize, bridge, message))
		{
			setError(message);
			return ASE_HWMalfunction;
		}
		for (Port& port : ports_)
		{
			if (port.endpoint == nullptr)
				continue;
			const size_t planeBytes = static_cast<size_t>(bufferSize) * port.container.bytes();
			for (int half = 0; half < 2; half++)
			{
				port.planes[half].assign(port.channels, std::vector<unsigned char>());
				port.planePointers[half].resize(port.channels);
				for (unsigned c = 0; c < port.channels; c++)
				{
					port.planes[half][c].assign(planeBytes, 0);
					port.planePointers[half][c] = port.planes[half][c].data();
				}
			}
		}
		bridge_.store(bridge, std::memory_order_release);
		host_.set(*callbacks);
		frames_ = bufferSize;
		for (long i = 0; i < numChannels; i++)
		{
			Port& port = bufferInfos[i].isInput ? ports_[1] : ports_[0];
			bufferInfos[i].buffers[0] = port.planes[0][static_cast<size_t>(bufferInfos[i].channelNum)].data();
			bufferInfos[i].buffers[1] = port.planes[1][static_cast<size_t>(bufferInfos[i].channelNum)].data();
		}
		samplePosition_.store(0, std::memory_order_relaxed);
		counters_ = Counters();
		counters_.bridge = bridge;
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
			if (port.endpoint == nullptr)
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
		if (!stopEvent_ || !startAckEvent_)
		{
			setError("The stream synchronization events are not available");
			return ASE_HWMalfunction;
		}
		if (running_.load(std::memory_order_acquire) && threadAlive_.load(std::memory_order_acquire))
			return ASE_OK;
		if (thread_.joinable())
		{
			stopRequested_.store(true, std::memory_order_release);
			SetEvent(stopEvent_.get());
			thread_.join();
		}
		stopRequested_.store(false, std::memory_order_release);
		ResetEvent(stopEvent_.get());
		threadAlive_.store(true, std::memory_order_release);
		startResult_.store(ASE_OK, std::memory_order_relaxed);
		try
		{
			thread_ = std::thread(&WasapiExclusiveTarget::streamThread, this);
		}
		catch (...)
		{
			threadAlive_.store(false, std::memory_order_release);
			running_.store(false, std::memory_order_release);
			setError("The stream thread could not be started");
			return ASE_HWMalfunction;
		}
		WaitForSingleObject(startAckEvent_.get(), INFINITE);
		const ASIOError result = static_cast<ASIOError>(startResult_.load(std::memory_order_acquire));
		if (result != ASE_OK)
		{
			thread_.join();
			running_.store(false, std::memory_order_release);
			threadAlive_.store(false, std::memory_order_release);
			return result;
		}
		running_.store(true, std::memory_order_release);
		return ASE_OK;
	}

	ASIOError WasapiExclusiveTarget::stop()
	{
		stopRequested_.store(true, std::memory_order_release);
		if (stopEvent_)
			SetEvent(stopEvent_.get());
		if (thread_.joinable())
			thread_.join();
		running_.store(false, std::memory_order_release);
		return ASE_OK;
	}

	void WasapiExclusiveTarget::fillTimeInfo(ASIOTime& time) const noexcept
	{
		splitInt64(samplePosition_.load(std::memory_order_relaxed), time.timeInfo.samplePosition.hi, time.timeInfo.samplePosition.lo);
		splitInt64(Clock::nanoseconds(), time.timeInfo.systemTime.hi, time.timeInfo.systemTime.lo);
		time.timeInfo.sampleRate = static_cast<ASIOSampleRate>(rate_);
		time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
	}

	void WasapiExclusiveTarget::drainCapture(Port& port) noexcept
	{
		if (port.endpoint == nullptr)
			return;
		UINT32 packet = 0;
		while (SUCCEEDED(port.endpoint->getNextPacketSize(&packet)) && packet > 0)
		{
			BYTE* data = nullptr;
			UINT32 frames = 0;
			DWORD flags = 0;
			HRESULT hr = port.endpoint->getCaptureBuffer(&data, &frames, &flags);
			if (FAILED(hr))
			{
				noteDeviceResult(hr);
				break;
			}
			port.queue.push(data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
			hr = port.endpoint->releaseCaptureBuffer(frames);
			if (FAILED(hr))
			{
				noteDeviceResult(hr);
				break;
			}
		}
	}

	// Keeps the result the stream thread judges the event by: a device that
	// went away outranks any other failure of the same event.
	void WasapiExclusiveTarget::noteDeviceResult(HRESULT hr) noexcept
	{
		if (FAILED(hr) && deviceResult_ != AUDCLNT_E_DEVICE_INVALIDATED)
			deviceResult_ = hr;
	}

	void WasapiExclusiveTarget::commitOutput(long half) noexcept
	{
		Port& port = ports_[0];
		if (port.endpoint == nullptr)
		{
			committed_.store(true, std::memory_order_release);
			return;
		}
		// The ASIO period goes into the device block at its slot; the block
		// is handed to the device once every ASIO period of the device
		// period is in it (one write per event with bridge 1).
		const size_t frameBytes = static_cast<size_t>(port.channels) * port.container.bytes();
		const wasapi::OutputStager::Step step = port.stager.stage();
		wasapi::interleave(port.planePointers[half].data(), port.channels, port.container.bytes(), static_cast<unsigned>(frames_),
			port.block.data() + static_cast<size_t>(step.slot) * static_cast<size_t>(frames_) * frameBytes);
		committed_.store(true, std::memory_order_release);
		if (step.writeFrames == 0)
			return;
		const UINT32 deviceFrames = step.writeFrames;
		BYTE* data = nullptr;
		HRESULT hr = port.endpoint->getRenderBuffer(deviceFrames, &data);
		if (FAILED(hr))
		{
			counters_.outputMisses++;
			noteDeviceResult(hr);
			return;
		}
		std::memcpy(data, port.block.data(), static_cast<size_t>(deviceFrames) * frameBytes);
		hr = port.endpoint->releaseRenderBuffer(deviceFrames, 0);
		noteDeviceResult(hr);
	}

	void WasapiExclusiveTarget::servePeriod(long half) noexcept
	{
		Port& in = ports_[1];
		if (in.endpoint != nullptr)
		{
			drainCapture(in);
			if (!in.queue.take(in.planePointers[half].data(), static_cast<unsigned>(frames_)))
				counters_.inputUnderruns++;
		}

		pendingHalf_.store(half, std::memory_order_release);
		committed_.store(false, std::memory_order_release);
		if (host_.supportsTimeInfo)
		{
			ASIOTime time = {};
			fillTimeInfo(time);
			host_.calls.bufferSwitchTimeInfo(&time, half, ASIOTrue);
		}
		else if (host_.calls.bufferSwitch != nullptr)
		{
			host_.calls.bufferSwitch(half, ASIOTrue);
		}
		if (!committed_.load(std::memory_order_acquire))
			commitOutput(half);
		pendingHalf_.store(-1, std::memory_order_release);
		samplePosition_.fetch_add(static_cast<uint64_t>(frames_), std::memory_order_relaxed);
		counters_.periods++;
	}

	// One silent device period ahead of the host's first real one, so the
	// device has something to play from the first event.
	void WasapiExclusiveTarget::primeOutput() noexcept
	{
		Port& out = ports_[0];
		if (out.endpoint == nullptr)
			return;
		const UINT32 deviceFrames = static_cast<UINT32>(frames_) * out.bridge;
		BYTE* data = nullptr;
		if (SUCCEEDED(out.endpoint->getRenderBuffer(deviceFrames, &data)))
			out.endpoint->releaseRenderBuffer(deviceFrames, AUDCLNT_BUFFERFLAGS_SILENT);
	}

	// Starts the recording endpoint, then the playback one, the order the
	// stream relies on; on a failure the error text names the endpoint and
	// the other is not started.
	bool WasapiExclusiveTarget::startEndpoints() noexcept
	{
		Port& out = ports_[0];
		Port& in = ports_[1];
		if (in.endpoint != nullptr)
		{
			const HRESULT hr = in.endpoint->start();
			if (FAILED(hr))
			{
				std::snprintf(errorMessage_, sizeof(errorMessage_), "The recording endpoint could not start (0x%08lx)", static_cast<unsigned long>(hr));
				return false;
			}
		}
		if (out.endpoint != nullptr)
		{
			const HRESULT hr = out.endpoint->start();
			if (FAILED(hr))
			{
				std::snprintf(errorMessage_, sizeof(errorMessage_), "The playback endpoint could not start (0x%08lx)", static_cast<unsigned long>(hr));
				return false;
			}
		}
		return true;
	}

	// Reopens both streams with a device period of `factor` ASIO periods.
	// Stream thread only, between events. The ASIO buffers stay where they
	// are; only the device side changes, and the host hears about the new
	// latency through kAsioLatenciesChanged when it supports the message.
	bool WasapiExclusiveTarget::rebridge(unsigned factor) noexcept
	{
		Port& out = ports_[0];
		Port& in = ports_[1];
		if (out.endpoint != nullptr)
			out.endpoint->stop();
		if (in.endpoint != nullptr)
			in.endpoint->stop();
		for (Port& port : ports_)
			if (port.endpoint != nullptr)
				port.closeStream();
		char message[errorMessageBytes] = {};
		if (!prepareStreams(frames_, factor, message))
		{
			setError(message);
			return false;
		}
		primeOutput();
		if (!startEndpoints())
			return false;
		bridge_.store(factor, std::memory_order_release);
		counters_.bridge = factor;
		if (host_.calls.asioMessage != nullptr
			&& host_.calls.asioMessage(kAsioSelectorSupported, kAsioLatenciesChanged, nullptr, nullptr) == 1)
			host_.calls.asioMessage(kAsioLatenciesChanged, 0, nullptr, nullptr);
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
		bool started = startEndpoints();

		// Some drivers accept a small period and then signal at their own
		// coarser cycle (a virtual cable: every 10 ms against a 5.8 ms
		// period), consuming a whole cycle's worth per event; one ASIO
		// period per event then leaves the rest unplayed. The first events
		// tell: when their typical spacing is well over the period, the
		// device side is reopened at the smallest multiple of the ASIO
		// period that covers the cycle, and every event serves that many
		// ASIO periods back to back. The host keeps its buffer size; the
		// stream keeps its audio; only the latency grows, and is reported.
		// A buffer below the device's minimum already opened that way
		// (createBuffers), and the calibration starts from that bridge.
		// EAPO_WASAPI_FORCE_BRIDGE=<n> takes that decision up front, for
		// exercising the path on a driver that does not need it. The
		// decision itself is wasapi::BridgeCalibrator's.
		int forcedBridge = 0;
		{
			wchar_t value[8] = {};
			if (GetEnvironmentVariableW(L"EAPO_WASAPI_FORCE_BRIDGE", value, 8) > 0)
				forcedBridge = _wtoi(value);
		}
		const uint64_t periodNanos = rate_ != 0 ? static_cast<uint64_t>(static_cast<double>(frames_) * 1e9 / static_cast<double>(rate_)) : 0;
		wasapi::BridgeCalibrator calibrator(periodNanos, bridge_.load(std::memory_order_acquire), forcedBridge);
		if (started && calibrator.forced() && !rebridge(calibrator.factor()))
			started = false;
		startResult_.store(started ? ASE_OK : ASE_HWMalfunction, std::memory_order_release);
		SetEvent(startAckEvent_.get());

		HANDLE clock = out.endpoint != nullptr ? out.endpoint->event() : in.endpoint->event();
		long half = 0;
		uint64_t devicePeriodNanos = periodNanos * bridge_.load(std::memory_order_acquire);
		uint64_t previousEvent = 0;
		uint64_t intervalSum = 0, intervalCount = 0, intervalMax = 0, serviceMax = 0;
		wasapi::StreamHealthJudge health;
		const bool enteredLoop = started;
		while (started && !stopRequested_.load(std::memory_order_acquire))
		{
			const HANDLE waits[2] = {stopEvent_.get(), clock};
			const DWORD waited = WaitForMultipleObjects(2, waits, FALSE, wasapi::eventWaitMs);
			if (waited == WAIT_OBJECT_0)
				break;
			if (waited == WAIT_TIMEOUT)
			{
				// A device that was unplugged stops signalling; after a
				// few silent waits the stream ends and the host is asked
				// for a reset below, as for any other device failure.
				if (health.judgeWait(true, S_OK) == wasapi::StreamHealth::DeviceLost)
				{
					setError("The device stopped signalling; it may have been unplugged");
					break;
				}
				continue;
			}
			if (waited != WAIT_OBJECT_0 + 1)
				break;
			const uint64_t now = Clock::nanoseconds();
			bool calibrationDue = false;
			if (previousEvent != 0)
			{
				const uint64_t interval = now - previousEvent;
				if (devicePeriodNanos != 0 && interval > devicePeriodNanos + devicePeriodNanos * 3 / 4)
					counters_.slowEvents++;
				intervalSum += interval;
				intervalCount++;
				if (interval > intervalMax)
					intervalMax = interval;
				calibrationDue = calibrator.addSpacing(interval);
			}
			previousEvent = now;
			deviceResult_ = S_OK;
			const unsigned bridge = bridge_.load(std::memory_order_acquire);
			for (unsigned k = 0; k < bridge; k++)
			{
				servePeriod(half);
				half ^= 1;
			}
			const uint64_t served = Clock::nanoseconds() - now;
			if (served > serviceMax)
				serviceMax = served;
			if (health.judgeWait(false, deviceResult_) == wasapi::StreamHealth::DeviceLost)
			{
				setError("The device went away; it may have been unplugged");
				break;
			}

			if (calibrationDue && calibrator.factor() > bridge_.load(std::memory_order_acquire))
			{
				// A device that will not reopen ends the stream, as a
				// device that vanished would; the host sees no more switches.
				if (!rebridge(calibrator.factor()))
					break;
				clock = out.endpoint != nullptr ? out.endpoint->event() : in.endpoint->event();
				devicePeriodNanos = periodNanos * calibrator.factor();
				previousEvent = 0;
				// The reopened period is judged again: a cable whose cycle
				// is 441 frames took a 448-frame period (64 x 7) and then
				// signalled every second cycle, which left half of each
				// event unplayed. Each reopen only raises the bridge, up to
				// the cap, so this ends.
				calibrator = wasapi::BridgeCalibrator(periodNanos, calibrator.factor(), 0);
			}
		}
		counters_.eventIntervalAvgUs = intervalCount != 0 ? intervalSum / intervalCount / 1000 : 0;
		counters_.eventIntervalMaxUs = intervalMax / 1000;
		counters_.serviceMaxUs = serviceMax / 1000;
		counters_.bridge = bridge_.load(std::memory_order_acquire);
		if (enteredLoop && !stopRequested_.load(std::memory_order_acquire) && host_.calls.asioMessage != nullptr
			&& host_.calls.asioMessage(kAsioSelectorSupported, kAsioResetRequest, nullptr, nullptr) == 1)
			host_.calls.asioMessage(kAsioResetRequest, 0, nullptr, nullptr);

		if (out.endpoint != nullptr)
			out.endpoint->stop();
		if (in.endpoint != nullptr)
			in.endpoint->stop();
		if (task != nullptr)
			AvRevertMmThreadCharacteristics(task);
		if (timerRequest == TIMERR_NOERROR)
			timeEndPeriod(1);
		threadId_.store(0, std::memory_order_release);
		if (SUCCEEDED(com))
			CoUninitialize();
		threadAlive_.store(false, std::memory_order_release);
	}
}
