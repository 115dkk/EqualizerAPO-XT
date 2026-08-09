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
	std::lock_guard<std::mutex> lock(initMutex);

	if (!module)
	{
		wstring libPath = getLoadPath();
		if (GetFileAttributesW(libPath.c_str()) == INVALID_FILE_ATTRIBUTES)
			return FILE_NOT_FOUND;
		module.reset(LoadLibraryW(libPath.c_str()));
		if (!module)
		{
			unsigned short arch = getFileArchitecture(libPath);
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

wstring AbstractLibrary::getLoadPath()
{
	// By default the load path equals the library path. Subclasses such as
	// VSTPluginLibrary override this to resolve the binary inside a .vst3 bundle.
	return getLibPath();
}

unsigned short AbstractLibrary::getFileArchitecture(const wstring& filePath)
{
	unsigned short result = 0;

	winutil::UniqueHandle file(CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (file)
	{
		winutil::UniqueHandle mapping(CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
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
