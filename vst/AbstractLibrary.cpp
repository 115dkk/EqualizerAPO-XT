/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

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
#include <Imagehlp.h>
#include "AbstractLibrary.h"
#include "services/logging/Logging.h"

using std::wstring;

AbstractLibrary::~AbstractLibrary()
{
	if (module)
	{
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(module.get(), path, MAX_PATH);

		module.reset();

		TraceF(L"Unloaded library %s", path);
	}
}

int AbstractLibrary::initialize()
{
	const auto target = ConfigFileReference::library(L"", getLibPath(), L"");
	if (!target.refusal.empty())
		return LOADING_FAILED;
	return initialize(target.path);
}

int AbstractLibrary::initialize(const JudgedPath& path)
{
	std::lock_guard<std::mutex> lock(initMutex);

	if (!module)
	{
		const wstring& libPath = path.path();
		HANDLE file = path.leaf();
		if (file == nullptr)
			return FILE_NOT_FOUND;
		winutil::UniqueHandle held;
		const DWORD holdError = holdForLoad(file, held);
		if (holdError != ERROR_SUCCESS)
		{
			LogF(L"Not loading %s: %s (error %lu)", libPath.c_str(), holdError == ERROR_SHARING_VIOLATION
				? L"another program has it open for writing" : holdError == ERROR_BAD_EXE_FORMAT
				? L"the file is empty" : L"it could not be held for loading", holdError);
			return LOADING_FAILED;
		}
		// LoadLibraryW parses the name again rather than using the handles the
		// judgment holds, so what it opens is the judged file only if no
		// component of that name can change until it does. The name is the
		// judged one with every link already resolved, and each component is
		// held (see ConfigPathPolicy). Measured on NTFS and on a Dev Drive
		// (ReFS), Windows 11 22621, with the symbolic-link privilege enabled:
		// - a held component cannot be renamed or deleted, and a folder the
		//   engine holds attributes-only cannot be renamed while anything
		//   beneath it is held (MoveFileW and POSIX rename, error 5);
		// - a folder with anything in it cannot become a junction or a
		//   symbolic link (FSCTL_SET_REPARSE_POINT, error 145), and every held
		//   folder holds the next component;
		// - a file with data cannot become a symbolic link (error 4392), but
		//   an empty one can, even while held.
		// So the leaf is the one component that could still turn into a link
		// here: emptied through write sharing, then given reparse data (which
		// needs only FILE_WRITE_ATTRIBUTES, outside share checks). holdForLoad
		// denies writers from here to the load and refuses an empty file.
		module.reset(LoadLibraryW(libPath.c_str()));
		if (!module)
		{
			unsigned short arch = getFileArchitecture(file);
#if defined(_M_ARM64)
			const unsigned short expectedArch = IMAGE_FILE_MACHINE_ARM64;
#elif defined(_WIN64)
			const unsigned short expectedArch = IMAGE_FILE_MACHINE_AMD64;
#else
			const unsigned short expectedArch = IMAGE_FILE_MACHINE_I386;
#endif
			if (arch != 0 && arch != expectedArch)
				return WRONG_ARCHITECTURE;

			return LOADING_FAILED;
		}

		if (!loadFunctions())
		{
			customUninitialize();
			module.reset();
			return FUNCTIONS_MISSING;
		}

		int res = customInitialize();
		if (res < 0)
		{
			customUninitialize();
			module.reset();
			return res;
		}

		TraceF(L"Loaded library %s", libPath.c_str());

		return 1;
	}

	return 0;
}

int AbstractLibrary::customInitialize()
{
	// overwrite if needed
	return 0;
}

void AbstractLibrary::customUninitialize() noexcept
{
	// overwrite if needed
}

DWORD AbstractLibrary::holdForLoad(HANDLE leaf, winutil::UniqueHandle& held)
{
	// A plug-in on the configuration's own share (the one remote exception)
	// keeps the server's rules; nothing below was measured there.
	FILE_REMOTE_PROTOCOL_INFO remote = {};
	if (GetFileInformationByHandleEx(leaf, FileRemoteProtocolInfo, &remote, sizeof(remote)))
		return ERROR_SUCCESS;
	// Nothing on a volume without reparse points (FAT32, exFAT) can become a
	// link, so a plug-in there loads as it always did.
	DWORD volumeFlags = 0;
	if (GetVolumeInformationByHandleW(leaf, nullptr, 0, nullptr, nullptr, &volumeFlags, nullptr, 0)
		&& (volumeFlags & FILE_SUPPORTS_REPARSE_POINTS) == 0)
		return ERROR_SUCCESS;
	// Reopened through the handle, so no name is parsed. Without write
	// sharing it fails while any writer is open, which LoadLibraryW would
	// also fail on: an image section needs a file nobody can write.
	held.reset(ReOpenFile(leaf, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ,
		FILE_FLAG_OPEN_REPARSE_POINT));
	if (!held)
		return GetLastError();
	// Checked after the reopen: from here the file cannot be emptied, and a
	// file with data cannot be made a link.
	FILE_ATTRIBUTE_TAG_INFO tag = {};
	FILE_STANDARD_INFO standard = {};
	if (!GetFileInformationByHandleEx(held.get(), FileAttributeTagInfo, &tag, sizeof(tag))
		|| !GetFileInformationByHandleEx(held.get(), FileStandardInfo, &standard, sizeof(standard)))
		return GetLastError();
	if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
		|| ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(tag.ReparseTag)))
		return ERROR_CANT_ACCESS_FILE;
	if (standard.EndOfFile.QuadPart == 0)
		return ERROR_BAD_EXE_FORMAT;
	return ERROR_SUCCESS;
}

wstring AbstractLibrary::getLoadPath()
{
	// By default the load path equals the library path. Subclasses such as
	// VSTPluginLibrary override this to resolve the binary inside a .vst3 bundle.
	return getLibPath();
}

unsigned short AbstractLibrary::getFileArchitecture(HANDLE file)
{
	unsigned short result = 0;

	if (file)
	{
		winutil::UniqueHandle mapping(CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr));
		if (mapping)
		{
			winutil::UniqueMappedView view(MapViewOfFileEx(mapping.get(), FILE_MAP_READ, 0, 0, 0, nullptr));
			if (view)
			{
				PIMAGE_NT_HEADERS ntHeaders = ImageNtHeader(view.get());
				if (ntHeaders != nullptr)
					result = ntHeaders->FileHeader.Machine;
			}
		}
	}

	return result;
}
