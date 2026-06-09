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
#include "helpers/StringHelper.h"
#include "helpers/VSTPluginLibrary.h"
#include "helpers/LogHelper.h"
#include "VSTPluginCommand.h"
#include "VSTPluginFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "VSTPluginFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::VSTPlugin, VSTPluginFilterFactory)

using std::shared_ptr;
using std::unordered_map;
using std::vector;
using std::wstring;

vector<IFilter*> VSTPluginFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	VSTPluginFilter* filter = nullptr;

	if (command == L"VSTPlugin")
	{
		// The parameter parse now lives in VSTPluginCommand::parse so the Editor
		// GUI factory can reuse it without building a throwaway filter. The load
		// decision and error handling below are unchanged: when configPath is set
		// the binary is still loaded via library->initialize() with the same
		// messages and the same create/skip outcome as before.
		VSTPluginCommand cmd = VSTPluginCommand::parse(configPath, parameters);
		shared_ptr<VSTPluginLibrary> library = cmd.libraryPath.empty() ? nullptr : VSTPluginLibrary::getInstance(cmd.libraryPath);
		const wstring& chunkData = cmd.chunkData;
		const unordered_map<wstring, float>& paramMap = cmd.paramMap;

		if (library != nullptr)
		{
			bool create = true;
			if (configPath != L"")
			{
				create = false;
				TraceF(L"Adding VST plugin %s", library->getLibPath().c_str());
				int res = library->initialize();
				if (res < 0)
				{
#ifdef _WIN64
					int bitDepth = 64;
#else
					int bitDepth = 32;
#endif
					if (res == AbstractLibrary::FILE_NOT_FOUND)
						LogF(L"File %s not found", library->getLibPath());
					else if (res == AbstractLibrary::LOADING_FAILED)
						LogF(L"Library %s could not be loaded", library->getLibPath());
					else if (res == AbstractLibrary::FUNCTIONS_MISSING)
						LogF(L"Library %s does not contain needed functions", library->getLibPath());
					else if (res == AbstractLibrary::WRONG_ARCHITECTURE)
						LogF(L"Library %s has wrong architecture, must be %d-bit", library->getLibPath(), bitDepth);
				}
				else
				{
					create = true;
				}
			}

			if (create)
			{
				filter = MemoryHelper::construct<VSTPluginFilter>(library, chunkData, paramMap);
			}
		}
	}

	if (filter == nullptr)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}
