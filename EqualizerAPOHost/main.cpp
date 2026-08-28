/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	EqualizerAPOHost.exe: the engine host for ASIO streams
	(docs/architecture/asio-host-study.md, sections 9-10). One per session
	endpoint, started on demand by the first wrapper that needs it, serving
	one FilterEngine pair per stream on its own Pro Audio thread, and leaving
	`--linger` milliseconds after the last stream ends so a DAW's buffer-size
	change does not respawn it. With `--resident` (the Device Selector's
	start-at-boot option writes a Run value with it) it never leaves on idle.

	Control: a message-mode named pipe. A wrapper writes one HostOpenRequest
	naming the ring it mapped; the host opens the ring and its events by
	name, answers with its pid, and serves. Everything after that goes
	through the ring; the pipe connection is closed.

	No window, no console: it logs to EqualizerAPOHost.log under the user's
	EqualizerAPO log folder, next to the Editor's.
*/

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

#include "asio/EngineHostCore.h"
#include "asio/HostProtocol.h"
#include "runtime/ipc/StreamRing.h"
#include "services/logging/Logging.h"

// After windows.h (through the ring header): shellapi.h needs its types.
#include <shellapi.h>

using eapo::asio::HostOpenReply;
using eapo::asio::HostOpenRequest;
using eapo::asio::HostOpenStatus;

namespace
{
	struct Arguments
	{
		std::wstring endpoint;
		uint32_t lingerMs = 60000;
		bool resident = false;
	};

	Arguments parseArguments(int argc, wchar_t** argv)
	{
		Arguments a;
		for (int i = 1; i < argc; i++)
		{
			const std::wstring key = argv[i];
			if (key == L"--endpoint" && i + 1 < argc)
				a.endpoint = argv[++i];
			else if (key == L"--linger" && i + 1 < argc)
				a.lingerMs = static_cast<uint32_t>(std::wcstoul(argv[++i], nullptr, 10));
			else if (key == L"--resident")
				a.resident = true;
		}
		if (a.endpoint.empty())
			a.endpoint = eapo::asio::HostNames::defaultEndpoint();
		return a;
	}

	struct Server
	{
		Arguments arguments;
		std::atomic<int> activeStreams{0};
		std::atomic<ULONGLONG> idleSince{0};
		std::atomic<bool> stopping{false};

		static bool overlappedIo(HANDLE pipe, bool write, void* buffer, DWORD bytes)
		{
			OVERLAPPED overlapped = {};
			overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			DWORD transferred = 0;
			BOOL ok = write ? WriteFile(pipe, buffer, bytes, &transferred, &overlapped) : ReadFile(pipe, buffer, bytes, &transferred, &overlapped);
			if (!ok && GetLastError() == ERROR_IO_PENDING)
			{
				if (WaitForSingleObject(overlapped.hEvent, 5000) == WAIT_OBJECT_0)
					ok = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
				else
					CancelIo(pipe);
			}
			CloseHandle(overlapped.hEvent);
			return ok && transferred == bytes;
		}

		void serve(HostOpenRequest request)
		{
			HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, request.ringName);
			void* base = mapping != nullptr ? MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, request.ringBytes) : nullptr;
			const wchar_t* const suffixes[5] = {L"work0", L"work1", L"done0", L"done1", L"ready"};
			HANDLE events[5] = {};
			bool ok = base != nullptr;
			for (int i = 0; ok && i < 5; i++)
			{
				events[i] = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, eapo::asio::HostNames::event(request.ringName, suffixes[i]).c_str());
				ok = events[i] != nullptr;
			}
			HANDLE producer = OpenProcess(SYNCHRONIZE, FALSE, request.producerPid);
			if (ok)
			{
				eapo::ipc::RingSync sync;
				sync.work[0] = events[0];
				sync.work[1] = events[1];
				sync.done[0] = events[2];
				sync.done[1] = events[3];
				sync.ready = events[4];
				sync.peer = producer;
				eapo::ipc::RingConsumer consumer(base, request.ringBytes, sync);
				eapo::asio::ServeOptions options;
				options.configPath = request.configPath;
				options.proAudio = true;
				options.spinPeriods = 1.0;
				options.publishFacts = true;
				eapo::asio::EngineHostCore::serveStream(consumer, options, GetCurrentProcessId());
			}
			else
			{
				LogFStatic(L"ASIO host: the ring %s could not be opened (error %lu)", request.ringName, GetLastError());
			}
			if (producer != nullptr)
				CloseHandle(producer);
			for (HANDLE event : events)
			{
				if (event != nullptr)
					CloseHandle(event);
			}
			if (base != nullptr)
				UnmapViewOfFile(base);
			if (mapping != nullptr)
				CloseHandle(mapping);
			if (activeStreams.fetch_sub(1) == 1)
				idleSince = GetTickCount64();
		}

		void handleConnection(HANDLE pipe)
		{
			HostOpenRequest request;
			HostOpenReply reply;
			reply.hostPid = GetCurrentProcessId();
			if (!overlappedIo(pipe, false, &request, sizeof(request)))
				return;
			if (request.version != eapo::asio::hostProtocolVersion)
				reply.status = static_cast<uint32_t>(HostOpenStatus::BadVersion);
			else if (stopping.load())
				reply.status = static_cast<uint32_t>(HostOpenStatus::ShuttingDown);
			else if (request.ringBytes < eapo::ipc::ringHeaderBytes || request.ringName[0] == L'\0')
				reply.status = static_cast<uint32_t>(HostOpenStatus::BadRing);
			else
				reply.status = static_cast<uint32_t>(HostOpenStatus::Accepted);
			request.ringName[127] = L'\0';
			request.configPath[259] = L'\0';
			if (reply.status == static_cast<uint32_t>(HostOpenStatus::Accepted))
			{
				if (request.lingerMs > arguments.lingerMs)
					arguments.lingerMs = request.lingerMs;
				activeStreams.fetch_add(1);
				std::thread([this, request] { serve(request); }).detach();
			}
			overlappedIo(pipe, true, &reply, sizeof(reply));
			FlushFileBuffers(pipe);
		}

		int run()
		{
			const std::wstring pipeName = eapo::asio::HostNames::pipe(arguments.endpoint);
			idleSince = GetTickCount64();
			LogFStatic(L"ASIO host: listening on %s, linger %u ms%s", pipeName.c_str(), arguments.lingerMs,
				arguments.resident ? L", resident" : L"");
			for (;;)
			{
				HANDLE pipe = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
					PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
					sizeof(HostOpenReply), sizeof(HostOpenRequest), 0, nullptr);
				if (pipe == INVALID_HANDLE_VALUE)
				{
					LogFStatic(L"ASIO host: the control pipe could not be created (error %lu)", GetLastError());
					return 2;
				}
				OVERLAPPED overlapped = {};
				overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				bool connected = false;
				if (ConnectNamedPipe(pipe, &overlapped) || GetLastError() == ERROR_PIPE_CONNECTED)
					connected = true;
				while (!connected)
				{
					const DWORD waited = WaitForSingleObject(overlapped.hEvent, 1000);
					if (waited == WAIT_OBJECT_0)
					{
						connected = true;
						break;
					}
					if (!arguments.resident && activeStreams.load() == 0 && GetTickCount64() - idleSince.load() >= arguments.lingerMs)
					{
						stopping = true;
						CancelIo(pipe);
						break;
					}
				}
				CloseHandle(overlapped.hEvent);
				if (connected)
					handleConnection(pipe);
				DisconnectNamedPipe(pipe);
				CloseHandle(pipe);
				if (stopping.load())
					break;
			}
			// stopping is only set with no active stream, so nothing is
			// being served here; a stream that arrived in between was refused
			// with ShuttingDown.
			LogFStatic(L"ASIO host: idle for %u ms, leaving", arguments.lingerMs);
			return 0;
		}
	};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	Server server;
	server.arguments = parseArguments(argc, argv);
	if (argv != nullptr)
		LocalFree(argv);
	Logging::useUserFile(L"EqualizerAPOHost.log", false, false, false);

	// One host per endpoint: a second start (two wrappers racing) leaves at
	// once and the first keeps serving.
	HANDLE owner = CreateMutexW(nullptr, TRUE, eapo::asio::HostNames::owner(server.arguments.endpoint).c_str());
	if (owner == nullptr)
		return 2;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CloseHandle(owner);
		return 0;
	}
	const int result = server.run();
	ReleaseMutex(owner);
	CloseHandle(owner);
	return result;
}
