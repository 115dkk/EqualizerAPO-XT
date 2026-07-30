/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors

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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Win32Resource.h"

// Opens a file, retrying while another process holds it open for writing
// (ERROR_SHARING_VIOLATION). Returns an empty owning handle on any other
// error with that error code in lastError.
inline winutil::UniqueHandle openFileWithSharingRetry(
	const wchar_t* path, DWORD desiredAccess, DWORD shareMode, DWORD creationDisposition, DWORD& lastError)
{
	lastError = ERROR_SUCCESS;

	for (;;)
	{
		winutil::UniqueHandle file(CreateFileW(
			path, desiredAccess, shareMode, nullptr, creationDisposition, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (file)
			return file;

		lastError = GetLastError();
		if (lastError != ERROR_SHARING_VIOLATION)
			return {};

		// file is being written, so wait
		Sleep(1);
	}
}

// Configuration readers must not block QSaveFile's atomic replacement of the
// path they are reading. FILE_SHARE_DELETE lets the old file stay alive
// through this handle while the directory entry is replaced; omitting
// FILE_SHARE_WRITE still prevents an in-place writer from changing the bytes
// underneath the reader.
inline winutil::UniqueHandle openFileForReplaceableReadWithRetry(
	const wchar_t* path, DWORD& lastError)
{
	return openFileWithSharingRetry(
		path,
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_DELETE,
		OPEN_EXISTING,
		lastError);
}
