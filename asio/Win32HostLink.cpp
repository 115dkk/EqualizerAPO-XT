/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/Win32HostLink.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "asio/HostProtocol.h"
#include "platform/windows/NamedPipeSecurity.h"

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

		std::string utf8(const std::wstring& text)
		{
			const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
			std::string result(length > 0 ? length : 0, '\0');
			if (length > 0)
				WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
			return result;
		}

		// A request or a reply carries no size of its own to wait for, so a
		// host that accepted the connection and then hung used to hang the
		// application's open call with it. One overlapped transfer that gives
		// up at the deadline, cancelling the I/O before it returns.
		bool transfer(HANDLE pipe, bool write, void* buffer, DWORD bytes, ULONGLONG deadline, DWORD& error)
		{
			winutil::UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!event)
			{
				error = GetLastError();
				return false;
			}
			OVERLAPPED overlapped = {};
			overlapped.hEvent = event.get();
			const BOOL started = write ? WriteFile(pipe, buffer, bytes, nullptr, &overlapped)
				: ReadFile(pipe, buffer, bytes, nullptr, &overlapped);
			if (!started && GetLastError() != ERROR_IO_PENDING)
			{
				error = GetLastError();
				return false;
			}
			const ULONGLONG now = GetTickCount64();
			const DWORD wait = now >= deadline ? 0 : static_cast<DWORD>((std::min<ULONGLONG>)(deadline - now, INFINITE - 1));
			DWORD transferred = 0;
			if (WaitForSingleObject(event.get(), wait) != WAIT_OBJECT_0)
			{
				CancelIoEx(pipe, &overlapped);
				GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
				error = ERROR_TIMEOUT;
				return false;
			}
			if (!GetOverlappedResult(pipe, &overlapped, &transferred, FALSE))
			{
				error = GetLastError();
				return false;
			}
			error = transferred == bytes ? ERROR_SUCCESS : ERROR_INVALID_DATA;
			return transferred == bytes;
		}

		// The reply may need more time than a cold start left over, so the
		// exchange always gets at least this much.
		constexpr ULONGLONG exchangeFloorMs = 2000;
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

	std::wstring Win32HostLink::hostExecutable(const StreamOptions& options, const std::wstring& moduleDirectory)
	{
		if (!options.daemonExePath.empty())
			return options.daemonExePath;
		const std::wstring beside = moduleDirectory + L"\\EqualizerAPOHost.exe";
		if (fileExists(beside))
			return beside;
		const size_t slash = moduleDirectory.find_last_of(L"\\/");
		if (slash != std::wstring::npos)
		{
			const std::wstring parent = moduleDirectory.substr(0, slash) + L"\\EqualizerAPOHost.exe";
			if (fileExists(parent))
				return parent;
		}
		return beside;
	}

	bool Win32HostLink::spawnHost(const std::wstring& endpoint, const StreamOptions& options, std::string& error)
	{
		const std::wstring exe = hostExecutable(options, moduleDirectory());
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

	bool Win32HostLink::connectToHost(const std::wstring& endpoint, const StreamOptions& options, ULONGLONG deadline, HANDLE& pipe,
		std::string& error)
	{
		const std::wstring pipeName = HostNames::pipe(endpoint);
		bool spawned = false;
		for (;;)
		{
			pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (pipe != INVALID_HANDLE_VALUE)
			{
				DWORD mode = PIPE_READMODE_MESSAGE;
				SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

				// The pipe name is global, so whoever serves it has to be the
				// host this link would have started before it is told
				// anything.
				DWORD identityError = ERROR_SUCCESS;
				const std::wstring server = winutil::pipes::serverImagePath(pipe, identityError);
				const std::wstring expected = hostExecutable(options, moduleDirectory());
				if (server.empty() || !winutil::pipes::sameFile(server, expected))
				{
					CloseHandle(pipe);
					pipe = INVALID_HANDLE_VALUE;
					error = server.empty() ? describe("the program serving the EQ APO XT engine host pipe could not be identified", identityError)
						: "EQ APO XT engine host pipe is held by another program (" + utf8(server) + ")";
					return false;
				}
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

		for (unsigned i = 0; i < RingEvents::count; i++)
		{
			const RingEvents::Entry& entry = RingEvents::table[i];
			objects_.events[i] = CreateEventW(nullptr, entry.manualReset ? TRUE : FALSE, FALSE, HostNames::event(ringName, entry.suffix).c_str());
			if (objects_.events[i] == nullptr)
			{
				error = describe("the stream events could not be created", GetLastError());
				close(session);
				return false;
			}
		}
		// The peer (the host's process handle) follows once the host answered.
		session.sync = RingEvents::toSync(objects_.events, nullptr);

		HANDLE pipe = INVALID_HANDLE_VALUE;
		const ULONGLONG deadline = GetTickCount64() + options.readyTimeoutMs;
		if (!connectToHost(endpoint, options, deadline, pipe, error))
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
		HostOpenReply reply;
		const ULONGLONG exchangeDeadline = (std::max)(deadline, GetTickCount64() + exchangeFloorMs);
		DWORD last = ERROR_SUCCESS;
		const bool exchanged = transfer(pipe, true, &request, sizeof(request), exchangeDeadline, last)
			&& transfer(pipe, false, &reply, sizeof(reply), exchangeDeadline, last);
		CloseHandle(pipe);
		if (!exchanged)
		{
			error = last == ERROR_TIMEOUT ? "EQ APO XT engine host did not answer the stream request in time"
				: describe("EQ APO XT engine host did not accept the stream", last);
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
