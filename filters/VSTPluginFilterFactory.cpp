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
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"
#include "services/logging/Logging.h"
#include "VSTPluginCommand.h"
#include "VSTPluginFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "VSTPluginFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::VSTPlugin, VSTPluginFilterFactory, L"VSTPlugin")

using std::shared_ptr;
using std::wstring;

FilterVector VSTPluginFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	FilterPtr filter;

	if (command == L"VSTPlugin")
	{
		const VSTPluginCommand pluginCommand = VSTPluginCommand::parse(configPath, parameters);
		if (!pluginCommand.valid)
			return reportParseError(command, pluginCommand.error);

		shared_ptr<VSTPluginLibrary> library = pluginCommand.libraryPath.empty()
			? nullptr : VSTPluginLibrary::getInstance(pluginCommand.libraryPath);

		if (library == nullptr)
			return reportParseError(command, L"expected Library followed by the path of a plugin");

		bool create = true;
		if (configPath != L"")
		{
			create = false;
			TraceF(L"Adding VST plugin %s", library->getLibPath().c_str());
			int res = library->initialize();
			if (res < 0)
			{
				// These four were already diagnosed, but only into the log. They
				// are reported now so the Editor can mark the line: a plugin of
				// the wrong architecture is the single most common VST mistake,
				// and it is invisible on screen.
				if (res == AbstractLibrary::FILE_NOT_FOUND)
					return reportParseError(command, L"the plugin \"" + library->getLibPath() + L"\" was not found");
				if (res == AbstractLibrary::LOADING_FAILED)
					return reportParseError(command, L"the plugin \"" + library->getLibPath() + L"\" could not be loaded");
				if (res == AbstractLibrary::FUNCTIONS_MISSING)
					return reportParseError(command, L"\"" + library->getLibPath() + L"\" is not a VST plugin: it does not export the entry points");
#ifdef _WIN64
				const wstring bitDepth = L"64";
#else
				const wstring bitDepth = L"32";
#endif
				if (res == AbstractLibrary::WRONG_ARCHITECTURE)
					return reportParseError(command, L"the plugin \"" + library->getLibPath()
						+ L"\" is built for the other architecture; a " + bitDepth + L"-bit plugin is needed here");
				return reportParseError(command, L"the plugin \"" + library->getLibPath() + L"\" could not be initialised");
			}
			create = true;
		}

		if (create)
		{
			// At runtime initialize() has inspected the loaded module's actual ABI.
			// Explicit layouts affect VST3 only; a loaded VST2 module quietly takes
			// the existing VST2 path. Parser-only callers retain the contract so a
			// future Editor can inspect it without loading the binary here.
			if (pluginCommand.hasBusContract && (configPath.empty() || library->isVST3()))
				filter = makeFilter<VSTPluginFilter>(library, pluginCommand.chunkData,
					pluginCommand.paramMap, pluginCommand.busContract,
					pluginCommand.inputChannels, pluginCommand.outputChannels);
			else
				filter = makeFilter<VSTPluginFilter>(library, pluginCommand.chunkData,
					pluginCommand.paramMap, pluginCommand.stereoInput);
		}
	}

	if (filter == nullptr)
		return {};
	return singleFilter(std::move(filter));
}
