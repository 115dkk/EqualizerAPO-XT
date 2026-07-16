/*
	This file is part of EqualizerAPO, a system-wide equalizer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

// Minimal, dependency-free RAII helpers for consuming COM in the
// non-realtime initialization paths. These deliberately avoid WRL and ATL so
// the header can also be used from code that is shared with the Qt tools
// (Common.lib), which do not link ATL. The realtime APOProcess path does not
// use COM, so nothing here ever runs on the audio thread.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <propidl.h>

#include <utility>

namespace winutil
{
	// Copyable COM smart pointer. Copying performs AddRef so the reference
	// count stays balanced; the previous hand-rolled local versions relied on
	// never being copied and would have double-released on an accidental copy.
	template<typename T>
	class ComPtr
	{
	public:
		ComPtr() noexcept = default;

		ComPtr(const ComPtr& other) noexcept
			: ptr(other.ptr)
		{
			if (ptr != nullptr)
				ptr->AddRef();
		}

		ComPtr(ComPtr&& other) noexcept
			: ptr(std::exchange(other.ptr, nullptr))
		{
		}

		ComPtr& operator=(const ComPtr& other) noexcept
		{
			if (this != &other)
			{
				if (other.ptr != nullptr)
					other.ptr->AddRef();
				reset();
				ptr = other.ptr;
			}
			return *this;
		}

		ComPtr& operator=(ComPtr&& other) noexcept
		{
			if (this != &other)
			{
				reset();
				ptr = std::exchange(other.ptr, nullptr);
			}
			return *this;
		}

		~ComPtr()
		{
			reset();
		}

		T* operator->() const noexcept { return ptr; }
		T* get() const noexcept { return ptr; }
		operator T*() const noexcept { return ptr; }
		explicit operator bool() const noexcept { return ptr != nullptr; }

		// Releases any held interface and hands back the address of the
		// internal pointer for use as an out-parameter.
		T** put() noexcept
		{
			reset();
			return &ptr;
		}

		void reset() noexcept
		{
			if (ptr != nullptr)
			{
				ptr->Release();
				ptr = nullptr;
			}
		}

	private:
		T* ptr = nullptr;
	};

	// Balances CoInitialize on the constructing thread. Declare this before any
	// ComPtr members so those interfaces are released before CoUninitialize runs
	// during reverse-order member destruction.
	class ComApartment
	{
	public:
		ComApartment() noexcept
			: result(CoInitialize(nullptr))
		{
		}

		explicit ComApartment(DWORD flags) noexcept
			: result(CoInitializeEx(nullptr, flags))
		{
		}

		~ComApartment()
		{
			if (SUCCEEDED(result))
				CoUninitialize();
		}

		ComApartment(const ComApartment&) = delete;
		ComApartment& operator=(const ComApartment&) = delete;
		ComApartment(ComApartment&&) = delete;
		ComApartment& operator=(ComApartment&&) = delete;

		HRESULT status() const noexcept { return result; }
		bool isUsable() const noexcept
		{
			return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
		}

	private:
		HRESULT result;
	};

	// RAII wrapper for a PROPVARIANT: PropVariantInit on construction,
	// PropVariantClear on destruction (even when an early return is taken).
	class PropVariant
	{
	public:
		PropVariant() noexcept { PropVariantInit(&value); }
		~PropVariant() { PropVariantClear(&value); }

		PropVariant(const PropVariant&) = delete;
		PropVariant& operator=(const PropVariant&) = delete;

		PROPVARIANT* operator&() noexcept { return &value; }
		const PROPVARIANT& get() const noexcept { return value; }
		const PROPVARIANT* operator->() const noexcept { return &value; }

	private:
		PROPVARIANT value;
	};

	// RAII wrapper for a block returned by a COM call that must be released
	// with CoTaskMemFree (e.g. IAudioClient::GetMixFormat).
	template<typename T>
	class CoTaskMem
	{
	public:
		CoTaskMem() noexcept = default;
		~CoTaskMem() { reset(); }

		CoTaskMem(const CoTaskMem&) = delete;
		CoTaskMem& operator=(const CoTaskMem&) = delete;

		T* operator->() const noexcept { return ptr; }
		T* get() const noexcept { return ptr; }
		operator T*() const noexcept { return ptr; }
		explicit operator bool() const noexcept { return ptr != nullptr; }

		T** put() noexcept
		{
			reset();
			return &ptr;
		}

		void reset() noexcept
		{
			if (ptr != nullptr)
			{
				CoTaskMemFree(ptr);
				ptr = nullptr;
			}
		}

	private:
		T* ptr = nullptr;
	};
}
