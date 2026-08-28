/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/Win32HostLink.h"

#include <atomic>
#include <cstring>

#include "asio/HostProtocol.h"

namespace eapo::asio
{
	namespace
	{
		std::atomic<uint32_t> ringSerial{0};

		std::string describe(const char* what, DWORD error)
		{
			return std::string(what) + " (error " + std::to_string(error) + ")";
		}

		bool fileExists(const std::wstring& path)
		{
			const DWORD attributes = GetFileAttributesW(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}
	}

	std::wstring Win32HostLink::moduleDirectory()
	{
		HMODULE module = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&Win32HostLink::moduleDirectory), &module);
		wchar_t path[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
		std::wstring result(path, length);
		const size_t slash = result.find_last_of(L"\\/");
		return slash == std::wstring::npos ? L"." : result.substr(0, slash);
	}

	bool Win32HostLink::spawnHost(const std::wstring& endpoint, const StreamOptions& options, std::string& error)
	{
		std::wstring exe = options.daemonExePath.empty() ? moduleDirectory() + L"\\EqualizerAPOHost.exe" : options.daemonExePath;
		if (!fileExists(exe))
		{
			error = "EQ APO XT engine host executable is missing";
			return false;
		}
		std::wstring commandLine = L"\"" + exe + L"\" --endpoint \"" + endpoint + L"\" --linger " + std::to_wstring(options.lingerMs);
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		// A DAW's own environment and working directory are not the host's
		// business; only the executable's folder matters for its DLLs.
		const std::wstring directory = exe.substr(0, exe.find_last_of(L"\\/"));
		if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
			directory.c_str(), &startup, &process))
		{
			error = describe("EQ APO XT engine host could not be started", GetLastError());
			return false;
		}
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return true;
	}

	bool Win32HostLink::connectToHost(const std::wstring& endpoint, const StreamOptions& options, HANDLE& pipe, std::string& error)
	{
		const std::wstring pipeName = HostNames::pipe(endpoint);
		const ULONGLONG deadline = GetTickCount64() + options.readyTimeoutMs;
		bool spawned = false;
		for (;;)
		{
			pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			if (pipe != INVALID_HANDLE_VALUE)
			{
				DWORD mode = PIPE_READMODE_MESSAGE;
				SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
				return true;
			}
			const DWORD last = GetLastError();
			if (last == ERROR_PIPE_BUSY)
			{
				WaitNamedPipeW(pipeName.c_str(), 500);
			}
			else if (last == ERROR_FILE_NOT_FOUND)
			{
				if (!spawned)
				{
					if (!spawnHost(endpoint, options, error))
						return false;
					spawned = true;
				}
				Sleep(50);
			}
			else
			{
				error = describe("EQ APO XT engine host pipe could not be opened", last);
				return false;
			}
			if (GetTickCount64() >= deadline)
			{
				error = spawned ? "EQ APO XT engine host started but did not answer in time" : "EQ APO XT engine host did not answer in time";
				return false;
			}
		}
	}

	bool Win32HostLink::open(const StreamFormat& format, const StreamOptions& options, HostSession& session, std::string& error)
	{
		close(session);
		const std::wstring endpoint = options.daemonEndpoint.empty() ? HostNames::defaultEndpoint() : options.daemonEndpoint;
		const uint32_t pid = GetCurrentProcessId();
		const std::wstring ringName = HostNames::ring(endpoint, pid, ++ringSerial);
		const uint32_t ringBytes = eapo::ipc::RingGeometry::totalBytes(format);

		objects_.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, ringBytes, ringName.c_str());
		if (objects_.mapping == nullptr)
		{
			error = describe("the stream ring could not be created", GetLastError());
			return false;
		}
		session.ringBase = MapViewOfFile(objects_.mapping, FILE_MAP_ALL_ACCESS, 0, 0, ringBytes);
		if (session.ringBase == nullptr)
		{
			error = describe("the stream ring could not be mapped", GetLastError());
			close(session);
			return false;
		}
		session.ringBytes = ringBytes;
		std::memset(session.ringBase, 0, ringBytes);

		const wchar_t* const suffixes[5] = {L"work0", L"work1", L"done0", L"done1", L"ready"};
		for (int i = 0; i < 5; i++)
		{
			objects_.events[i] = CreateEventW(nullptr, i == 4 ? TRUE : FALSE, FALSE, HostNames::event(ringName, suffixes[i]).c_str());
			if (objects_.events[i] == nullptr)
			{
				error = describe("the stream events could not be created", GetLastError());
				close(session);
				return false;
			}
		}
		session.sync.work[0] = objects_.events[0];
		session.sync.work[1] = objects_.events[1];
		session.sync.done[0] = objects_.events[2];
		session.sync.done[1] = objects_.events[3];
		session.sync.ready = objects_.events[4];

		HANDLE pipe = INVALID_HANDLE_VALUE;
		if (!connectToHost(endpoint, options, pipe, error))
		{
			close(session);
			return false;
		}

		HostOpenRequest request;
		request.producerPid = pid;
		request.ringBytes = ringBytes;
		request.lingerMs = options.lingerMs;
		wcsncpy_s(request.ringName, ringName.c_str(), _TRUNCATE);
		wcsncpy_s(request.configPath, options.configPath.c_str(), _TRUNCATE);
		DWORD transferred = 0;
		HostOpenReply reply;
		const bool exchanged = WriteFile(pipe, &request, sizeof(request), &transferred, nullptr) && transferred == sizeof(request)
			&& ReadFile(pipe, &reply, sizeof(reply), &transferred, nullptr) && transferred == sizeof(reply);
		const DWORD last = GetLastError();
		CloseHandle(pipe);
		if (!exchanged)
		{
			error = describe("EQ APO XT engine host did not accept the stream", last);
			close(session);
			return false;
		}
		if (reply.status != static_cast<uint32_t>(HostOpenStatus::Accepted))
		{
			error = reply.status == static_cast<uint32_t>(HostOpenStatus::BadVersion)
				? "EQ APO XT engine host speaks another protocol version; reinstall both"
				: "EQ APO XT engine host refused the stream";
			close(session);
			return false;
		}
		session.hostPid = reply.hostPid;
		// The peer handle turns a host crash into Gone; when it cannot be
		// opened (a different integrity level) the ring still works, the
		// wrapper just learns of a crash through the deadline instead.
		session.sync.peer = OpenProcess(SYNCHRONIZE, FALSE, reply.hostPid);
		return true;
	}

	void Win32HostLink::close(HostSession& session) noexcept
	{
		if (session.ringBase != nullptr)
			UnmapViewOfFile(session.ringBase);
		if (session.sync.peer != nullptr)
			CloseHandle(session.sync.peer);
		for (HANDLE& event : objects_.events)
		{
			if (event != nullptr)
				CloseHandle(event);
			event = nullptr;
		}
		if (objects_.mapping != nullptr)
			CloseHandle(objects_.mapping);
		objects_.mapping = nullptr;
		session = HostSession();
	}
}
