/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/HostControlServer.h"

#include <algorithm>
#include <utility>

#include "platform/windows/NamedPipeSecurity.h"
#include "services/logging/Logging.h"

namespace eapo::asio
{
	HostActivity::HostActivity() noexcept
	{
		idleSince_.store(GetTickCount64(), std::memory_order_relaxed);
	}

	void HostActivity::started() noexcept
	{
		activeStreams_.fetch_add(1, std::memory_order_acq_rel);
	}

	void HostActivity::finished() noexcept
	{
		idleSince_.store(GetTickCount64(), std::memory_order_release);
		activeStreams_.fetch_sub(1, std::memory_order_acq_rel);
	}

	int HostActivity::activeStreams() const noexcept
	{
		return activeStreams_.load(std::memory_order_acquire);
	}

	ULONGLONG HostActivity::idleSince() const noexcept
	{
		return idleSince_.load(std::memory_order_acquire);
	}

	HostControlServer::HostControlServer(HostControlOptions options, HostActivity& activity, OpenHandler onOpen)
		: options_(std::move(options)), activity_(activity), onOpen_(std::move(onOpen))
	{
		if (options_.endpoint.empty())
			options_.endpoint = HostNames::defaultEndpoint();
		lingerMs_.store((std::min)(options_.lingerMs, maxHostLingerMs), std::memory_order_relaxed);
	}

	HostControlServer::~HostControlServer()
	{
		requestStop();
	}

	void HostControlServer::requestStop() noexcept
	{
		stopping_.store(true, std::memory_order_release);
	}

	HostOpenReply HostControlServer::handleConnection(HANDLE pipe) noexcept
	{
		HostOpenRequest request;
		HostOpenReply reply;
		reply.status = static_cast<uint32_t>(HostOpenStatus::BadRing);
		reply.hostPid = GetCurrentProcessId();
		const ULONGLONG deadline = GetTickCount64() + options_.transferTimeoutMs;
		if (pipeTransfer(pipe, false, &request, sizeof(request), deadline) != ERROR_SUCCESS)
			return reply;
		request.ringName[127] = L'\0';
		request.configPath[259] = L'\0';
		if (request.version != hostProtocolVersion)
			reply.status = static_cast<uint32_t>(HostOpenStatus::BadVersion);
		else if (stopping_.load(std::memory_order_acquire))
			reply.status = static_cast<uint32_t>(HostOpenStatus::ShuttingDown);
		else if (request.ringBytes < eapo::ipc::ringHeaderBytes || request.ringName[0] == L'\0')
			reply.status = static_cast<uint32_t>(HostOpenStatus::BadRing);
		else
		{
			try
			{
				reply = onOpen_(request);
				if (reply.hostPid == 0)
					reply.hostPid = GetCurrentProcessId();
			}
			catch (...)
			{
				reply.status = static_cast<uint32_t>(HostOpenStatus::BadRing);
				reply.hostPid = GetCurrentProcessId();
			}
		}
		if (reply.status == static_cast<uint32_t>(HostOpenStatus::Accepted))
		{
			const uint32_t requested = (std::min)(request.lingerMs, maxHostLingerMs);
			uint32_t current = lingerMs_.load(std::memory_order_acquire);
			while (requested > current && !lingerMs_.compare_exchange_weak(current, requested, std::memory_order_acq_rel))
			{
			}
		}
		pipeTransfer(pipe, true, &reply, sizeof(reply), deadline);
		FlushFileBuffers(pipe);
		return reply;
	}

	int HostControlServer::run() noexcept
	{
		const std::wstring pipeName = HostNames::pipe(options_.endpoint);
		winutil::pipes::PipeSecurity security(winutil::pipes::userOnlySddl(winutil::pipes::currentUserSid()));
		if (!security.valid())
		{
			LogFStatic(L"ASIO host: the control pipe's security descriptor could not be built (error %lu)", security.error());
			return 2;
		}
		const auto createInstance = [&](bool first) {
			return CreateNamedPipeW(pipeName.c_str(),
				PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
				sizeof(HostOpenReply), sizeof(HostOpenRequest), 0, security.attributes());
		};

		LogFStatic(L"ASIO host: listening on %s, linger %u ms%s", pipeName.c_str(), lingerMs(),
			options_.resident ? L", resident" : L"");
		winutil::UniqueHandle pipe(createInstance(true));
		if (!pipe)
		{
			const DWORD error = GetLastError();
			if (error == ERROR_ACCESS_DENIED)
				LogFStatic(L"ASIO host: another program already holds %s", pipeName.c_str());
			else
				LogFStatic(L"ASIO host: the control pipe could not be created (error %lu)", error);
			return 2;
		}

		for (;;)
		{
			winutil::UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!event)
				return 2;
			OVERLAPPED overlapped = {};
			overlapped.hEvent = event.get();
			bool connected = ConnectNamedPipe(pipe.get(), &overlapped) != FALSE;
			const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
			if (connectError == ERROR_PIPE_CONNECTED)
				connected = true;
			else if (!connected && connectError != ERROR_IO_PENDING)
			{
				LogFStatic(L"ASIO host: the control pipe could not accept a connection (error %lu)", connectError);
				return 2;
			}

			DWORD transferred = 0;
			while (!connected)
			{
				const DWORD waited = WaitForSingleObject(event.get(), 1000);
				if (waited == WAIT_OBJECT_0)
				{
					connected = GetOverlappedResult(pipe.get(), &overlapped, &transferred, FALSE) != FALSE;
					if (!connected)
					{
						LogFStatic(L"ASIO host: the control pipe connection failed (error %lu)", GetLastError());
						return 2;
					}
					break;
				}
				if (waited == WAIT_FAILED)
				{
					CancelIoEx(pipe.get(), &overlapped);
					GetOverlappedResult(pipe.get(), &overlapped, &transferred, TRUE);
					return 2;
				}
				const bool idleExpired = !options_.resident && activity_.activeStreams() == 0
					&& GetTickCount64() - activity_.idleSince() >= lingerMs();
				if (stopping_.load(std::memory_order_acquire) || idleExpired)
				{
					stopping_.store(true, std::memory_order_release);
					CancelIoEx(pipe.get(), &overlapped);
					GetOverlappedResult(pipe.get(), &overlapped, &transferred, TRUE);
					break;
				}
			}
			if (connected)
				handleConnection(pipe.get());

			// Keep the name continuously owned: the next listening instance exists
			// before the connected one is disconnected.
			winutil::UniqueHandle next(stopping_.load(std::memory_order_acquire) ? INVALID_HANDLE_VALUE : createInstance(false));
			const DWORD nextError = GetLastError();
			if (next)
				instanceHandoffs_.fetch_add(1, std::memory_order_release);
			DisconnectNamedPipe(pipe.get());
			pipe.reset();
			if (stopping_.load(std::memory_order_acquire))
				break;
			if (!next)
			{
				LogFStatic(L"ASIO host: the control pipe could not be created (error %lu)", nextError);
				return 2;
			}
			pipe = std::move(next);
		}
		LogFStatic(L"ASIO host: idle for %u ms, leaving", lingerMs());
		return 0;
	}
}
