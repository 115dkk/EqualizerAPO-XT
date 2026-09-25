/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Who may open, and who may serve, the two named pipes this program uses
	(audit #348 TD-46).

	The device test pipe had a NULL DACL: every account could write to it,
	change its DACL, and add an instance of its own that would receive what
	audiodg sent to the Device Selector. The ASIO control pipe had the default
	descriptor and its client never asked who was serving it, so a program
	that took the name first would receive the wrapper's stream requests.

	Both servers now create their pipe with an explicit DACL and
	FILE_FLAG_FIRST_PIPE_INSTANCE, and keep an instance open between clients
	so the name is never free to take. The ASIO wrapper checks that the
	process serving the pipe is the engine host it would have started.

	Header-only because the pipes live in four binaries (the APO, the Device
	Selector, the ASIO wrapper and the engine host) and the wrapper links no
	Common.lib.
*/

#pragma once

#include <string>
#include <vector>

#include <windows.h>
#include <sddl.h>

#include "platform/windows/Win32Resource.h"

namespace winutil::pipes
{
	// The device test pipe. SYSTEM and Administrators get everything: the
	// elevated Device Selector serves the pipe and writes its own "stop", and
	// the CI's ApoHostProbe loads the APO as an administrator. audiodg.exe
	// runs the APO as LOCAL SERVICE and only has to write one message, so it
	// gets 0x12019b: FILE_GENERIC_READ with FILE_WRITE_DATA, FILE_WRITE_EA and
	// FILE_WRITE_ATTRIBUTES. That leaves out FILE_APPEND_DATA, which on a pipe
	// is FILE_CREATE_PIPE_INSTANCE, and WRITE_DAC and WRITE_OWNER, so the
	// audio service's account can send a message but cannot add an instance
	// that would receive what was meant for the Device Selector, nor change
	// who else may. NamedPipeSecurityTests checks the instance part against
	// Windows.
	inline constexpr wchar_t kDeviceTestServerSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x12019b;;;LS)";

	// What a device test client asks for: the one right it uses. CreateFileW
	// adds SYNCHRONIZE and FILE_READ_ATTRIBUTES on its own; both are inside
	// the LOCAL SERVICE grant above.
	inline constexpr DWORD kDeviceTestClientAccess = FILE_WRITE_DATA;

	// Opens the device test pipe to send one message. When every instance is
	// busy it waits up to a second for one, as both clients always did.
	inline UniqueHandle openDeviceTestClient(const std::wstring& fullPipeName)
	{
		UniqueHandle pipe(CreateFileW(fullPipeName.c_str(), kDeviceTestClientAccess, 0, nullptr, OPEN_EXISTING, 0, nullptr));
		if (!pipe && WaitNamedPipeW(fullPipeName.c_str(), 1000))
			pipe.reset(CreateFileW(fullPipeName.c_str(), kDeviceTestClientAccess, 0, nullptr, OPEN_EXISTING, 0, nullptr));
		return pipe;
	}

	// The ASIO control pipe: the user the engine host runs as, and SYSTEM.
	// The wrapper runs in the same user's DAW, elevated or not.
	inline std::wstring userOnlySddl(const std::wstring& userSid)
	{
		return L"D:P(A;;GA;;;" + userSid + L")(A;;GA;;;SY)";
	}

	// The SID of the user this process runs as, as text; empty when the token
	// cannot be read.
	inline std::wstring currentUserSid()
	{
		UniqueHandle token;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()))
			return std::wstring();

		DWORD size = 0;
		GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
		if (size == 0)
			return std::wstring();
		std::vector<unsigned char> buffer(size);
		if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size))
			return std::wstring();

		UniqueLocalPtr<wchar_t> text;
		if (!ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid, text.put()))
			return std::wstring();
		return std::wstring(text.get());
	}

	// Security attributes built from an SDDL string. The descriptor lives as
	// long as this object, so keep it alive across the CreateNamedPipeW call.
	class PipeSecurity
	{
	public:
		explicit PipeSecurity(const std::wstring& sddl)
		{
			if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, descriptor_.put(), nullptr))
			{
				error_ = GetLastError();
				return;
			}
			attributes_.nLength = sizeof(attributes_);
			attributes_.lpSecurityDescriptor = descriptor_.get();
			attributes_.bInheritHandle = FALSE;
		}

		PipeSecurity(const PipeSecurity&) = delete;
		PipeSecurity& operator=(const PipeSecurity&) = delete;

		bool valid() const
		{
			return static_cast<bool>(descriptor_);
		}

		// The Win32 error that made the descriptor fail, when valid() is false.
		DWORD error() const
		{
			return error_;
		}

		SECURITY_ATTRIBUTES* attributes()
		{
			return valid() ? &attributes_ : nullptr;
		}

	private:
		UniqueLocalPtr<void> descriptor_;
		SECURITY_ATTRIBUTES attributes_ = {};
		DWORD error_ = 0;
	};

	// True when the two paths name the same file. When both can be opened the
	// file identities are compared, which sees through case, short names and
	// junctions; otherwise the full paths are compared without regard to case
	// or slash direction.
	inline bool sameFile(const std::wstring& left, const std::wstring& right)
	{
		const auto identity = [](const std::wstring& path, BY_HANDLE_FILE_INFORMATION& info) {
			UniqueHandle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
			return file && GetFileInformationByHandle(file.get(), &info);
		};
		BY_HANDLE_FILE_INFORMATION leftInfo = {};
		BY_HANDLE_FILE_INFORMATION rightInfo = {};
		if (identity(left, leftInfo) && identity(right, rightInfo))
		{
			return leftInfo.dwVolumeSerialNumber == rightInfo.dwVolumeSerialNumber
				&& leftInfo.nFileIndexHigh == rightInfo.nFileIndexHigh
				&& leftInfo.nFileIndexLow == rightInfo.nFileIndexLow;
		}

		const auto full = [](const std::wstring& path) {
			std::wstring result(MAX_PATH, L'\0');
			DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(result.size()), result.data(), nullptr);
			if (length >= result.size())
			{
				result.resize(length);
				length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(result.size()), result.data(), nullptr);
			}
			result.resize(length < result.size() ? length : 0);
			for (wchar_t& c : result)
			{
				if (c == L'/')
					c = L'\\';
			}
			return result;
		};
		const std::wstring leftFull = full(left);
		const std::wstring rightFull = full(right);
		return !leftFull.empty()
			&& CompareStringOrdinal(leftFull.c_str(), static_cast<int>(leftFull.size()),
				rightFull.c_str(), static_cast<int>(rightFull.size()), TRUE) == CSTR_EQUAL;
	}

	// The executable of the process serving the other end of a client pipe
	// handle; empty, with error set, when it cannot be found out.
	inline std::wstring serverImagePath(HANDLE pipe, DWORD& error)
	{
		ULONG pid = 0;
		if (!GetNamedPipeServerProcessId(pipe, &pid))
		{
			error = GetLastError();
			return std::wstring();
		}
		UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
		if (!process)
		{
			error = GetLastError();
			return std::wstring();
		}
		std::wstring path(32768, L'\0');
		DWORD length = static_cast<DWORD>(path.size());
		if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length))
		{
			error = GetLastError();
			return std::wstring();
		}
		path.resize(length);
		error = ERROR_SUCCESS;
		return path;
	}
}
