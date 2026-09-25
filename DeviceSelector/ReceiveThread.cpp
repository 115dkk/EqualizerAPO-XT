/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2024  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "platform/windows/Win32Error.h"
#include "services/logging/Logging.h"
#include "platform/windows/Win32Resource.h"
#include "platform/windows/NamedPipeSecurity.h"
#include "devices/DeviceTestWire.h"
#include "ReceiveThread.h"

ReceiveThread::ReceiveThread(const std::wstring& pipeName)
	: pipeName(pipeName)
{
	thread = std::thread(&ReceiveThread::run, this);
}

ReceiveThread::~ReceiveThread()
{
	stop();
}

void ReceiveThread::stop()
{
	if (!thread.joinable())
		return;

	winutil::UniqueHandle pipe = winutil::pipes::openDeviceTestClient(L"\\\\.\\pipe\\" + pipeName);
	if (pipe)
	{
		DWORD bytesWritten;
		WriteFile(pipe.get(), devicetest::wire::kStopMessage, sizeof(devicetest::wire::kStopMessage) - 1, &bytesWritten, nullptr);
		FlushFileBuffers(pipe.get());
	}

	thread.join();
}

void ReceiveThread::run()
{
	try
	{
		// Who may touch the pipe is spelled out in NamedPipeSecurity.h; it used
		// to be a NULL DACL, which let any account add an instance of its own
		// and receive audiodg's messages (audit #348 TD-46).
		winutil::pipes::PipeSecurity security(winutil::pipes::kDeviceTestServerSddl);
		if (!security.valid())
			throw ReceiveException(L"Could not build the pipe's security descriptor: " + win32::errorMessage(security.error()));

		const std::wstring fullPipeName = L"\\\\.\\pipe\\" + pipeName;
		char buf[1024];
		const auto createInstance = [&](bool first) {
			return winutil::UniqueHandle(CreateNamedPipeW(fullPipeName.c_str(),
				PIPE_ACCESS_INBOUND | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				PIPE_UNLIMITED_INSTANCES, 0, sizeof(buf), 0, security.attributes()));
		};

		// The first instance has to be the first one of that name, so a
		// program that took the name earlier is found here instead of
		// receiving the test's messages.
		winutil::UniqueHandle pipe = createInstance(true);
		if (!pipe)
		{
			const DWORD error = GetLastError();
			if (error == ERROR_ACCESS_DENIED)
				throw ReceiveException(L"Another program already holds the pipe " + fullPipeName);
			throw ReceiveException(L"Could not create named pipe: " + win32::errorMessage(error));
		}

		while (true)
		{
			bool connected = ConnectNamedPipe(pipe.get(), nullptr);
			if (!connected)
				connected = GetLastError() == ERROR_PIPE_CONNECTED;

			if (connected)
			{
				DWORD bytesRead;
				bool ok = ReadFile(pipe.get(), buf, sizeof(buf), &bytesRead, nullptr);
				if (!ok || bytesRead == 0)
					throw ReceiveException(L"Could not read from pipe: " + win32::errorMessage(GetLastError()));

				std::string s(buf, bytesRead);
				if (s == devicetest::wire::kStopMessage)
					break;

				std::scoped_lock lock(mutex);
				answers.push_back(s);
				cond.notify_all();
			}

			// The next instance exists before this one closes, so the name is
			// never free for another program to take between two clients.
			winutil::UniqueHandle next = createInstance(false);
			if (!next)
				throw ReceiveException(L"Could not create named pipe: " + win32::errorMessage(GetLastError()));
			DisconnectNamedPipe(pipe.get());
			pipe = std::move(next);
		}
	}
	catch (const ReceiveException& e)
	{
		std::scoped_lock lock(mutex);
		caughtException = e;
		cond.notify_all();
	}
}
