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

	const std::wstring fullPipeName = L"\\\\.\\pipe\\" + pipeName;
	winutil::UniqueHandle pipe(CreateFileW(fullPipeName.c_str(),
		GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
	if (!pipe)
	{
		if (WaitNamedPipeW(fullPipeName.c_str(), 1000))
			pipe.reset(CreateFileW(fullPipeName.c_str(),
				GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
	}
	if (pipe)
	{
		DWORD bytesWritten;
		WriteFile(pipe.get(), "stop", 4, &bytesWritten, nullptr);
		FlushFileBuffers(pipe.get());
	}

	thread.join();
}

void ReceiveThread::run()
{
	try
	{
		winutil::UniqueLocalPtr<void> pSD(LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH));
		if (!pSD)
			throw ReceiveException(L"Could not allocate security descriptor: " + win32::errorMessage(GetLastError()));

		if (!InitializeSecurityDescriptor(pSD.get(), SECURITY_DESCRIPTOR_REVISION))
			throw ReceiveException(L"Could not initialize security descriptor: " + win32::errorMessage(GetLastError()));

		if (!SetSecurityDescriptorDacl(pSD.get(), TRUE, nullptr, FALSE))
			throw ReceiveException(L"Could not set security descriptor DACL: " + win32::errorMessage(GetLastError()));

		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof(sa);
		sa.lpSecurityDescriptor = pSD.get();
		sa.bInheritHandle = FALSE;

		char buf[1024];
		while (true)
		{
			winutil::UniqueHandle pipe(CreateNamedPipeW((L"\\\\.\\pipe\\" + pipeName).c_str(),
				PIPE_ACCESS_INBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				PIPE_UNLIMITED_INSTANCES, 0, sizeof(buf), 0, &sa));
			if (!pipe)
				throw ReceiveException(L"Could not create named pipe: " + win32::errorMessage(GetLastError()));

			bool connected = ConnectNamedPipe(pipe.get(), nullptr);
			if (!connected)
				connected = GetLastError() == ERROR_PIPE_CONNECTED;

			if (!connected)
				continue;

			SCOPE_EXIT{DisconnectNamedPipe(pipe.get());};

			DWORD bytesRead;
			bool ok = ReadFile(pipe.get(), buf, sizeof(buf), &bytesRead, nullptr);
			if (!ok || bytesRead == 0)
				throw ReceiveException(L"Could not read from pipe: " + win32::errorMessage(GetLastError()));

			std::string s(buf, bytesRead);
			if (s == "stop")
				break;

			std::scoped_lock lock(mutex);
			answers.push_back(s);
			cond.notify_all();
		}
	}
	catch (const ReceiveException& e)
	{
		std::scoped_lock lock(mutex);
		caughtException = e;
		cond.notify_all();
	}
}
