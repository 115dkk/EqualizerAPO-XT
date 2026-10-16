/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "asio/WasapiExclusiveTarget.h"

namespace asiotest
{
	class FakeEndpointClient final : public eapo::asio::wasapi::IEndpointClient
	{
	public:
		explicit FakeEndpointClient(DWORD cadenceMs, unsigned invalidateAfterReleases = 0)
			: cadenceMs_(cadenceMs), invalidateAfterReleases_(invalidateAfterReleases),
			event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)), stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr))
		{
		}

		~FakeEndpointClient() override
		{
			stop();
		}

		HRESULT isFormatSupported(const WAVEFORMATEX*) noexcept override
		{
			return S_OK;
		}

		HRESULT initialize(const WAVEFORMATEX* format, REFERENCE_TIME, UINT32 expectedFrames,
			UINT32& actualFrames, REFERENCE_TIME& latency) noexcept override
		{
			initializeCalls_.fetch_add(1, std::memory_order_relaxed);
			actualFrames = expectedFrames;
			latency = 0;
			const size_t bytes = format != nullptr ? static_cast<size_t>(expectedFrames) * format->nBlockAlign : 0;
			buffer_.assign(bytes, 0);
			return event_ && stop_ ? S_OK : E_FAIL;
		}

		void close() noexcept override
		{
			stop();
		}

		HRESULT start() noexcept override
		{
			if (!event_ || !stop_)
				return E_FAIL;
			if (ticker_.joinable())
				return S_OK;
			ResetEvent(stop_.get());
			try
			{
				ticker_ = std::thread([this] {
					for (;;)
					{
						if (WaitForSingleObject(stop_.get(), cadenceMs_) == WAIT_OBJECT_0)
							break;
						SetEvent(event_.get());
					}
				});
			}
			catch (...)
			{
				return E_FAIL;
			}
			startCalls_.fetch_add(1, std::memory_order_relaxed);
			return S_OK;
		}

		HRESULT stop() noexcept override
		{
			if (stop_)
				SetEvent(stop_.get());
			if (ticker_.joinable())
				ticker_.join();
			return S_OK;
		}

		HANDLE event() const noexcept override {return event_.get();}

		HRESULT getRenderBuffer(UINT32, BYTE** data) noexcept override
		{
			if (data == nullptr)
				return E_POINTER;
			*data = buffer_.data();
			return S_OK;
		}

		HRESULT releaseRenderBuffer(UINT32, DWORD) noexcept override
		{
			const unsigned count = releaseCalls_.fetch_add(1, std::memory_order_relaxed) + 1;
			return invalidateAfterReleases_ != 0 && count >= invalidateAfterReleases_
				? AUDCLNT_E_DEVICE_INVALIDATED : S_OK;
		}

		HRESULT getNextPacketSize(UINT32* frames) noexcept override
		{
			if (frames == nullptr)
				return E_POINTER;
			*frames = 0;
			return S_OK;
		}

		HRESULT getCaptureBuffer(BYTE**, UINT32*, DWORD*) noexcept override {return AUDCLNT_S_BUFFER_EMPTY;}
		HRESULT releaseCaptureBuffer(UINT32) noexcept override {return S_OK;}

		unsigned initializeCalls() const noexcept {return initializeCalls_.load(std::memory_order_relaxed);}
		unsigned startCalls() const noexcept {return startCalls_.load(std::memory_order_relaxed);}
		unsigned releaseCalls() const noexcept {return releaseCalls_.load(std::memory_order_relaxed);}

	private:
		DWORD cadenceMs_;
		unsigned invalidateAfterReleases_;
		winutil::UniqueHandle event_;
		winutil::UniqueHandle stop_;
		std::thread ticker_;
		std::vector<BYTE> buffer_;
		std::atomic<unsigned> initializeCalls_{0};
		std::atomic<unsigned> startCalls_{0};
		std::atomic<unsigned> releaseCalls_{0};
	};
}
