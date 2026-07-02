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

// Opens a file, retrying while another process holds it open for writing
// (ERROR_SHARING_VIOLATION). Returns INVALID_HANDLE_VALUE on any other
// error with that error code in lastError.
inline HANDLE openFileWithSharingRetry(const wchar_t* path, DWORD desiredAccess, DWORD shareMode, DWORD creationDisposition, DWORD& lastError)
{
	lastError = ERROR_SUCCESS;

	HANDLE hFile = INVALID_HANDLE_VALUE;
	while (hFile == INVALID_HANDLE_VALUE)
	{
		hFile = CreateFileW(path, desiredAccess, shareMode, nullptr, creationDisposition, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			lastError = GetLastError();
			if (lastError != ERROR_SHARING_VIOLATION)
				return INVALID_HANDLE_VALUE;

			// file is being written, so wait
			Sleep(1);
		}
	}

	return hFile;
}
