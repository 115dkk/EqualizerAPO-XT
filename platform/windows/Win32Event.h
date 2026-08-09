#pragma once

#include <system_error>

#include "platform/windows/Win32Resource.h"

class Win32Event
{
public:
	Win32Event(bool manualReset, bool initialState)
		: handle(CreateEventW(nullptr, manualReset, initialState, nullptr))
	{
		if (!handle)
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateEventW");
	}

	~Win32Event() = default;

	Win32Event(const Win32Event&) = delete;
	Win32Event& operator=(const Win32Event&) = delete;

	Win32Event(Win32Event&&) noexcept = default;
	Win32Event& operator=(Win32Event&&) noexcept = default;

	HANDLE get() const
	{
		return handle.get();
	}

	void set() const
	{
		SetEvent(handle.get());
	}

	void reset() const
	{
		ResetEvent(handle.get());
	}

	static DWORD waitAny(DWORD count, const HANDLE* handles, DWORD milliseconds = INFINITE)
	{
		return WaitForMultipleObjects(count, handles, false, milliseconds);
	}

	static DWORD waitOne(HANDLE handle, DWORD milliseconds)
	{
		return waitAny(1, &handle, milliseconds);
	}

private:
	winutil::UniqueHandle handle;
};
