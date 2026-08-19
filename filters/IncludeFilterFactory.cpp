/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

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
#include <filesystem>

#include "services/logging/Logging.h"
#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "IncludeCommand.h"
#include "IncludeFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Include, IncludeFilterFactory, L"Include")

using std::vector;
using std::wstring;

const int RECURSION_LIMIT = 100;

void IncludeFilterFactory::initialize(FilterEngine* engine)
{
	this->engine = engine;
}

FilterVector IncludeFilterFactory::startOfConfiguration()
{
	recursionDepth = -1;

	return {};
}

FilterVector IncludeFilterFactory::startOfFile(const wstring& configPath)
{
	recursionDepth++;

	return {};
}

FilterVector IncludeFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	IncludeCommand cmd;
	if (IncludeCommand::parse(command, parameters, cmd))
	{
		const wstring& value = cmd.path;

		// Same relative-path dialect as ConvolutionFilePath::resolve
		// (audit #275 TD-06): relative to the including config file, resolved
		// with std::filesystem. The previous Shlwapi variant went through a
		// MAX_PATH buffer, so a long path was silently truncated.
		namespace filesystem = std::filesystem;
		const filesystem::path includedPath(value);
		wstring includePath;
		if (includedPath.is_absolute())
			includePath = includedPath.lexically_normal().wstring();
		else
		{
			filesystem::path basePath(configPath);
			basePath.remove_filename();
			includePath = (basePath / includedPath).lexically_normal().wstring();
		}

		if (recursionDepth >= RECURSION_LIMIT)
			LogF(L"Skipping include of %s as recursion limit of %d has been reached", value.c_str(), RECURSION_LIMIT);
		else
			engine->loadConfigFile(includePath);
		command = L"";
	}

	return {};
}

FilterVector IncludeFilterFactory::endOfFile(const wstring& configPath)
{
	recursionDepth--;

	return {};
}
