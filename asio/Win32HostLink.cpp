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
#include "platform/windows/WindowsPath.h"

namespace eapo::asio
{
	namespace
	{
		std::atomic<uint32_t> ringSerial{0};

		std::string describe(const char* what, DWORD error)
		{
			return std::string(what) + " (error " + std::to_string(error) + ")";
		}

		std::string utf8(const std::wstring& text)
		{
			const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
			std::string result(length > 0 ? length : 0, '\0');
			if (length > 0)
				WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
			return result;
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
		if (pathutil::fileExists(beside))
			return beside;
		const size_t slash = moduleDirectory.find_last_of(L"\\/");
		if (slash != std::wstring::npos)
		{
			const std::wstring parent = moduleDirectory.substr(0, slash) + L"\\EqualizerAPOHost.exe";
			if (pathutil::fileExists(parent))
				return parent;
		}
		return beside;
	}

	bool Win32HostLink::spawnHost(const std::wstring& endpoint, const StreamOptions& options, std::string& error)
	{
		const std::wstring exe = hostExecutable(options, moduleDirectory());
		if (!pathutil::fileExists(exe))
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
		winutil::UniqueHandle threadHandle(process.hThread);
		winutil::UniqueHandle processHandle(process.hProcess);
		return true;
	}

	bool Win32HostLink::connectToHost(const std::wstring& endpoint, const StreamOptions& options, ULONGLONG deadline, winutil::UniqueHandle& pipe,
		std::string& error)
	{
		const std::wstring pipeName = HostNames::pipe(endpoint);
		bool spawned = false;
		for (;;)
		{
			pipe.reset(CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
			if (pipe)
			{
				DWORD mode = PIPE_READMODE_MESSAGE;
				SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr);

				// The pipe name is global, so whoever serves it has to be the
				// host this link would have started before it is told
				// anything.
				DWORD identityError = ERROR_SUCCESS;
				const std::wstring server = winutil::pipes::serverImagePath(pipe.get(), identityError);
				const std::wstring expected = hostExecutable(options, moduleDirectory());
				if (server.empty() || !winutil::pipes::sameFile(server, expected))
				{
					pipe.reset();
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

	std::unique_ptr<eapo::ipc::RingProducer> Win32HostLink::open(const StreamFormat& format, const StreamOptions& options,
		OpenReport& report)
	{
		close(nullptr);
		std::string error;
		const auto fail = [&]() -> std::unique_ptr<eapo::ipc::RingProducer> {
			report.status = OpenReport::Status::Unavailable;
			copyErrorMessage(report.message, error.c_str());
			close(nullptr);
			return nullptr;
		};
		const std::wstring endpoint = options.daemonEndpoint.empty() ? HostNames::defaultEndpoint() : options.daemonEndpoint;
		const uint32_t pid = GetCurrentProcessId();
		const std::wstring ringName = HostNames::ring(endpoint, pid, ++ringSerial);
		const uint32_t ringBytes = eapo::ipc::RingGeometry::totalBytes(format);

		objects_.mapping.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, ringBytes, ringName.c_str()));
		if (!objects_.mapping)
		{
			error = describe("the stream ring could not be created", GetLastError());
			return fail();
		}
		session_.ringBase = MapViewOfFile(objects_.mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, ringBytes);
		if (session_.ringBase == nullptr)
		{
			error = describe("the stream ring could not be mapped", GetLastError());
			return fail();
		}
		session_.ringBytes = ringBytes;
		std::memset(session_.ringBase, 0, ringBytes);

		for (unsigned i = 0; i < RingEvents::count; i++)
		{
			const RingEvents::Entry& entry = RingEvents::table[i];
			objects_.events[i].reset(CreateEventW(nullptr, entry.manualReset ? TRUE : FALSE, FALSE, HostNames::event(ringName, entry.suffix).c_str()));
			if (!objects_.events[i])
			{
				error = describe("the stream events could not be created", GetLastError());
				return fail();
			}
		}
		// The peer (the host's process handle) follows once the host answered.
		session_.sync = RingEvents::toSync(objects_.events, nullptr);

		winutil::UniqueHandle pipe;
		const ULONGLONG deadline = GetTickCount64() + options.readyTimeoutMs;
		if (!connectToHost(endpoint, options, deadline, pipe, error))
			return fail();

		HostOpenRequest request;
		request.producerPid = pid;
		request.ringBytes = ringBytes;
		request.lingerMs = options.lingerMs;
		wcsncpy_s(request.ringName, ringName.c_str(), _TRUNCATE);
		wcsncpy_s(request.configPath, options.configPath.c_str(), _TRUNCATE);
		HostOpenReply reply;
		const ULONGLONG exchangeDeadline = (std::max)(deadline, GetTickCount64() + exchangeFloorMs);
		DWORD last = pipeTransfer(pipe.get(), true, &request, sizeof(request), exchangeDeadline);
		if (last == ERROR_SUCCESS)
			last = pipeTransfer(pipe.get(), false, &reply, sizeof(reply), exchangeDeadline);
		const bool exchanged = last == ERROR_SUCCESS;
		pipe.reset();
		if (!exchanged)
		{
			error = last == ERROR_TIMEOUT ? "EQ APO XT engine host did not answer the stream request in time"
				: describe("EQ APO XT engine host did not accept the stream", last);
			return fail();
		}
		if (reply.status != static_cast<uint32_t>(HostOpenStatus::Accepted))
		{
			error = reply.status == static_cast<uint32_t>(HostOpenStatus::BadVersion)
				? "EQ APO XT engine host speaks another protocol version; reinstall both"
				: "EQ APO XT engine host refused the stream";
			return fail();
		}
		session_.hostPid = reply.hostPid;
		// The peer handle turns a host crash into Gone; when it cannot be
		// opened (a different integrity level) the ring still works, the
		// wrapper just learns of a crash through the deadline instead.
		objects_.peer.reset(OpenProcess(SYNCHRONIZE, FALSE, reply.hostPid));
		session_.sync.peer = objects_.peer.get();
		std::unique_ptr<eapo::ipc::RingProducer> producer = readyProducer(session_, format, options.readyTimeoutMs, report);
		if (producer == nullptr)
			close(nullptr);
		return producer;
	}

	void Win32HostLink::close(std::unique_ptr<eapo::ipc::RingProducer> producer) noexcept
	{
		if (producer != nullptr)
		{
			producer->close();
			producer.reset();
		}
		if (session_.ringBase != nullptr)
			UnmapViewOfFile(session_.ringBase);
		objects_.peer.reset();
		for (winutil::UniqueHandle& event : objects_.events)
			event.reset();
		objects_.mapping.reset();
		session_ = HostSession();
	}
}
