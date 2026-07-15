#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <system_error>
#include <utility>

class Win32Event
{
public:
	Win32Event(bool manualReset, bool initialState)
		: handle(CreateEventW(nullptr, manualReset, initialState, nullptr))
	{
		if (handle == nullptr)
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateEventW");
	}

	~Win32Event()
	{
		if (handle != nullptr)
			CloseHandle(handle);
	}

	Win32Event(const Win32Event&) = delete;
	Win32Event& operator=(const Win32Event&) = delete;

	Win32Event(Win32Event&& other) noexcept
		: handle(std::exchange(other.handle, nullptr))
	{
	}

	Win32Event& operator=(Win32Event&& other) noexcept
	{
		if (this != &other)
		{
			if (handle != nullptr)
				CloseHandle(handle);
			handle = std::exchange(other.handle, nullptr);
		}
		return *this;
	}

	HANDLE get() const
	{
		return handle;
	}

	void set() const
	{
		SetEvent(handle);
	}

	void reset() const
	{
		ResetEvent(handle);
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
	HANDLE handle;
};
