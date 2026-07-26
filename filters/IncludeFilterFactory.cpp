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
#include <Shlwapi.h>

#include "helpers/LogHelper.h"
#include "helpers/StringHelper.h"
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

		wstring includePath;
		if (PathIsRelativeW(value.c_str()))
		{
			wchar_t filePath[MAX_PATH];
			// Standard copy + truncate; _Copy_s is an MSVC-internal helper.
			const size_t copyLength = configPath.copy(filePath, MAX_PATH - 1);
			filePath[copyLength] = L'\0';
			PathRemoveFileSpecW(filePath);
			PathAppendW(filePath, value.c_str());
			includePath = filePath;
		}
		else
			includePath = value;

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
