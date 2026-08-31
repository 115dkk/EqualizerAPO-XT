/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

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

#pragma once

#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/Win32Resource.h"

// Opens a file, retrying while another process holds it open for writing
// (ERROR_SHARING_VIOLATION), backing off exponentially (1..20 ms) until the
// deadline passes or the optional cancel handle signals; both bounded exits
// return an empty handle with lastError still ERROR_SHARING_VIOLATION. Any
// other error returns an empty owning handle with that code in lastError.
inline winutil::UniqueHandle openFileWithSharingRetry(
	const wchar_t* path, DWORD desiredAccess, DWORD shareMode, DWORD creationDisposition, DWORD& lastError,
	HANDLE cancel = nullptr, DWORD deadlineMilliseconds = 10000)
{
	lastError = ERROR_SUCCESS;
	const ULONGLONG start = GetTickCount64();
	DWORD backoffMilliseconds = 1;

	for (;;)
	{
		winutil::UniqueHandle file(CreateFileW(
			path, desiredAccess, shareMode, nullptr, creationDisposition, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (file)
			return file;

		lastError = GetLastError();
		if (lastError != ERROR_SHARING_VIOLATION)
			return {};

		const ULONGLONG elapsed = GetTickCount64() - start;
		if (elapsed >= deadlineMilliseconds)
			return {};

		const DWORD waitMilliseconds = static_cast<DWORD>((std::min)(
			static_cast<ULONGLONG>(backoffMilliseconds), deadlineMilliseconds - elapsed));
		if (cancel != nullptr)
		{
			if (WaitForSingleObject(cancel, waitMilliseconds) == WAIT_OBJECT_0)
				return {};
		}
		else
		{
			Sleep(waitMilliseconds);
		}
		backoffMilliseconds = (std::min)(backoffMilliseconds * 2, 20UL);
	}
}
