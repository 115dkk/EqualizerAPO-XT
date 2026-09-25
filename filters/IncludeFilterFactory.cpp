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

#include "services/logging/Logging.h"
#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "IncludeCommand.h"
#include "ConfigFileReference.h"
#include "IncludeFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Include, IncludeFilterFactory, L"Include")

using std::vector;
using std::wstring;

const int RECURSION_LIMIT = 100;

void IncludeFilterFactory::initialize(FilterEngine* engine)
{
	ParseReportingFactory::initialize(engine);
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
		// Read by the rule every file a line names shares (audit #348 A1):
		// relative to the including config file, quotes and %VARIABLES% taken
		// the way Convolution always took them.
		const ConfigFileReference::Target file = ConfigFileReference::target(configPath, cmd.path);
		if (!file.refusal.empty())
			reportParseError(command, file.refusal);
		else if (file.path.empty())
			reportParseError(command, L"expected the path of a configuration file");
		else if (recursionDepth >= RECURSION_LIMIT)
			reportParseError(command, L"not included: includes are nested more than " + std::to_wstring(RECURSION_LIMIT) + L" deep");
		else
			engine->loadConfigFile(file.path);
		command = L"";
	}

	return {};
}

FilterVector IncludeFilterFactory::endOfFile(const wstring& configPath)
{
	recursionDepth--;

	return {};
}
