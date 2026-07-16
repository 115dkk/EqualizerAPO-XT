/*
	This file is part of EqualizerAPO, a system-wide equalizer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winreg.h>
#include <winsvc.h>
#include <authz.h>
#include <winhttp.h>

#include <utility>

namespace winutil
{
	template<typename Traits>
	class UniqueResource
	{
	public:
		using resource_type = typename Traits::resource_type;

		UniqueResource() noexcept = default;
		explicit UniqueResource(resource_type resource) noexcept
			: resource(resource)
		{
		}

		~UniqueResource()
		{
			reset();
		}

		UniqueResource(const UniqueResource&) = delete;
		UniqueResource& operator=(const UniqueResource&) = delete;

		UniqueResource(UniqueResource&& other) noexcept
			: resource(other.release())
		{
		}

		UniqueResource& operator=(UniqueResource&& other) noexcept
		{
			if (this != &other)
				reset(other.release());
			return *this;
		}

		resource_type get() const noexcept { return resource; }
		explicit operator bool() const noexcept { return Traits::isValid(resource); }

		resource_type release() noexcept
		{
			return std::exchange(resource, Traits::invalid());
		}

		void reset(resource_type replacement = Traits::invalid()) noexcept
		{
			if (resource != replacement && Traits::isValid(resource))
				Traits::close(resource);
			resource = replacement;
		}

		// For Win32 APIs that return a resource through an out-parameter.
		// Any currently owned resource is released before its address is exposed.
		resource_type* put() noexcept
		{
			reset();
			return &resource;
		}

	private:
		resource_type resource = Traits::invalid();
	};

	struct HandleTraits
	{
		using resource_type = HANDLE;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept
		{
			return value != nullptr && value != INVALID_HANDLE_VALUE;
		}
		static void close(resource_type value) noexcept { CloseHandle(value); }
	};

	struct RegistryKeyTraits
	{
		using resource_type = HKEY;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { RegCloseKey(value); }
	};

	struct ServiceHandleTraits
	{
		using resource_type = SC_HANDLE;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { CloseServiceHandle(value); }
	};

	struct ModuleTraits
	{
		using resource_type = HMODULE;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { FreeLibrary(value); }
	};

	struct FindHandleTraits
	{
		using resource_type = HANDLE;
		static resource_type invalid() noexcept { return INVALID_HANDLE_VALUE; }
		static bool isValid(resource_type value) noexcept
		{
			return value != nullptr && value != INVALID_HANDLE_VALUE;
		}
		static void close(resource_type value) noexcept { FindClose(value); }
	};

	struct ChangeNotificationTraits
	{
		using resource_type = HANDLE;
		static resource_type invalid() noexcept { return INVALID_HANDLE_VALUE; }
		static bool isValid(resource_type value) noexcept
		{
			return value != nullptr && value != INVALID_HANDLE_VALUE;
		}
		static void close(resource_type value) noexcept { FindCloseChangeNotification(value); }
	};

	struct MappedViewTraits
	{
		using resource_type = void*;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { UnmapViewOfFile(value); }
	};

	struct WindowHandleTraits
	{
		using resource_type = HWND;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { DestroyWindow(value); }
	};

	struct SidTraits
	{
		using resource_type = PSID;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { FreeSid(value); }
	};

	struct AuthzResourceManagerTraits
	{
		using resource_type = AUTHZ_RESOURCE_MANAGER_HANDLE;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { AuthzFreeResourceManager(value); }
	};

	struct AuthzContextTraits
	{
		using resource_type = AUTHZ_CLIENT_CONTEXT_HANDLE;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { AuthzFreeContext(value); }
	};

	struct WinHttpHandleTraits
	{
		using resource_type = HINTERNET;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(resource_type value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { WinHttpCloseHandle(value); }
	};

	template<typename T>
	struct LocalMemoryTraits
	{
		using resource_type = T*;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(const T* value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { LocalFree(value); }
	};

	template<typename T>
	struct CoTaskMemoryTraits
	{
		using resource_type = T*;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(const T* value) noexcept { return value != nullptr; }
		static void close(resource_type value) noexcept { CoTaskMemFree(value); }
	};

	template<typename T>
	struct ProcessHeapMemoryTraits
	{
		using resource_type = T*;
		static resource_type invalid() noexcept { return nullptr; }
		static bool isValid(const T* value) noexcept { return value != nullptr; }
		// cppcheck-suppress constParameterPointer ; HeapFree requires a mutable LPVOID even though it does not modify the block before releasing it
		static void close(resource_type value) noexcept
		{
			HeapFree(GetProcessHeap(), 0, value);
		}
	};

	using UniqueHandle = UniqueResource<HandleTraits>;
	using UniqueRegistryKey = UniqueResource<RegistryKeyTraits>;
	using UniqueServiceHandle = UniqueResource<ServiceHandleTraits>;
	using UniqueModule = UniqueResource<ModuleTraits>;
	using UniqueFindHandle = UniqueResource<FindHandleTraits>;
	using UniqueChangeNotification = UniqueResource<ChangeNotificationTraits>;
	using UniqueMappedView = UniqueResource<MappedViewTraits>;
	using UniqueWindowHandle = UniqueResource<WindowHandleTraits>;
	using UniqueSid = UniqueResource<SidTraits>;
	using UniqueAuthzResourceManager = UniqueResource<AuthzResourceManagerTraits>;
	using UniqueAuthzContext = UniqueResource<AuthzContextTraits>;
	using UniqueWinHttpHandle = UniqueResource<WinHttpHandleTraits>;

	template<typename T>
	using UniqueLocalPtr = UniqueResource<LocalMemoryTraits<T>>;

	template<typename T>
	using UniqueCoTaskMemPtr = UniqueResource<CoTaskMemoryTraits<T>>;

	template<typename T>
	using UniqueProcessHeapPtr = UniqueResource<ProcessHeapMemoryTraits<T>>;

	class UniqueProcessInformation
	{
	public:
		UniqueProcessInformation() noexcept = default;
		~UniqueProcessInformation() { reset(); }

		UniqueProcessInformation(const UniqueProcessInformation&) = delete;
		UniqueProcessInformation& operator=(const UniqueProcessInformation&) = delete;

		UniqueProcessInformation(UniqueProcessInformation&& other) noexcept
			: value(other.release())
		{
		}

		UniqueProcessInformation& operator=(UniqueProcessInformation&& other) noexcept
		{
			if (this != &other)
			{
				reset();
				value = other.release();
			}
			return *this;
		}

		PROCESS_INFORMATION* put() noexcept
		{
			reset();
			return &value;
		}

		const PROCESS_INFORMATION& get() const noexcept { return value; }
		HANDLE process() const noexcept { return value.hProcess; }
		HANDLE thread() const noexcept { return value.hThread; }
		DWORD processId() const noexcept { return value.dwProcessId; }

		PROCESS_INFORMATION release() noexcept
		{
			return std::exchange(value, PROCESS_INFORMATION{});
		}

		void reset() noexcept
		{
			if (HandleTraits::isValid(value.hThread))
				HandleTraits::close(value.hThread);
			if (HandleTraits::isValid(value.hProcess))
				HandleTraits::close(value.hProcess);
			value = {};
		}

	private:
		PROCESS_INFORMATION value{};
	};
}
