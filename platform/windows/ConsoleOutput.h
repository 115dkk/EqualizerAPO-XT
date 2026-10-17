/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <string>
#include "Win32Resource.h"

namespace winutil
{
// Header-only so the standalone downloader needs no project library.
class ParentConsole
{
public:
	ParentConsole() : attached(AttachConsole(ATTACH_PARENT_PROCESS) != FALSE)
	{
		if (attached)
			output.reset(CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, 0, nullptr));
	}
	~ParentConsole()
	{
		output.reset();
		if (attached)
			FreeConsole();
	}
	ParentConsole(const ParentConsole&) = delete;
	ParentConsole& operator=(const ParentConsole&) = delete;
	explicit operator bool() const {return attached;}
	void write(const std::wstring& text) const
	{
		if (output)
		{
			DWORD written = 0;
			WriteConsoleW(output.get(), text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
		}
	}
private:
	bool attached;
	UniqueHandle output;
};
}
