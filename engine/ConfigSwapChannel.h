#pragma once

#include <atomic>
#include <system_error>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/Win32Event.h"
#include "platform/windows/Win32Resource.h"

// Single-owner channel between the configuration-loading worker and the
// real-time audio thread. The producer permit prevents a pending config from
// being overwritten, while the ready flag supplies the release/acquire edge
// that makes the pointed-to configuration visible on weakly ordered CPUs.
template<typename Pointer>
class ConfigSwapChannel
{
public:
	ConfigSwapChannel()
		: shutdownEvent(true, false),
		  publishPermit(CreateSemaphoreW(nullptr, 1, 1, nullptr))
	{
		if (!publishPermit)
			throw std::system_error(static_cast<int>(GetLastError()),
				std::system_category(), "CreateSemaphoreW");
	}

	ConfigSwapChannel(const ConfigSwapChannel&) = delete;
	ConfigSwapChannel& operator=(const ConfigSwapChannel&) = delete;

	bool acquirePublishPermit(DWORD milliseconds = INFINITE)
	{
		HANDLE handles[2] = {shutdownEvent.get(), publishPermit.get()};
		const DWORD result = Win32Event::waitAny(2, handles, milliseconds);
		if (result == WAIT_OBJECT_0)
			return false;
		if (result == WAIT_OBJECT_0 + 1)
		{
			permitHeld.store(true, std::memory_order_release);
			return true;
		}
		if (result == WAIT_TIMEOUT)
			return false;
		throw std::system_error(static_cast<int>(GetLastError()),
			std::system_category(), "WaitForMultipleObjects");
	}

	// Safe on the RT thread: no C++ mutex, allocation, or wait.
	void releasePublishPermit() noexcept
	{
		if (permitHeld.exchange(false, std::memory_order_acq_rel))
			ReleaseSemaphore(publishPermit.get(), 1, nullptr);
	}

	void publish(Pointer config)
	{
		// Retire the configuration kept alive by the last RT transition on the
		// producer thread, never in the audio callback.
		previousConfig = Pointer();
		pendingConfig = std::move(config);
		pendingReady.store(true, std::memory_order_release);
	}

	bool hasPending() const noexcept
	{
		return pendingReady.load(std::memory_order_acquire);
	}

	Pointer& current() noexcept { return currentConfig; }
	const Pointer& current() const noexcept { return currentConfig; }
	Pointer& pending() noexcept { return pendingConfig; }
	const Pointer& pending() const noexcept { return pendingConfig; }

	void completeTransition() noexcept
	{
		previousConfig = std::move(currentConfig);
		currentConfig = std::move(pendingConfig);
		pendingReady.store(false, std::memory_order_relaxed);
		releasePublishPermit();
	}

	void reset(Pointer seed = Pointer()) noexcept
	{
		currentConfig = std::move(seed);
		pendingConfig = Pointer();
		previousConfig = Pointer();
		pendingReady.store(false, std::memory_order_relaxed);
		releasePublishPermit();
	}

	void shutdown() noexcept
	{
		shutdownEvent.set();
	}

	HANDLE shutdownHandle() const noexcept
	{
		return shutdownEvent.get();
	}

private:
	Pointer currentConfig;
	Pointer pendingConfig;
	Pointer previousConfig;
	std::atomic<bool> pendingReady{ false };
	std::atomic<bool> permitHeld{ false };
	Win32Event shutdownEvent;
	winutil::UniqueHandle publishPermit;
};
