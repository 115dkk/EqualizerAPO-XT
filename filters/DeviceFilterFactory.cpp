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
#include <mpParser.h>
#include "services/logging/LogHelper.h"
#include "text/StringHelper.h"
#ifndef NO_FILTERENGINE
#include "engine/FilterEngine.h"
#endif
#include "filters/FilterFactoryRegistry.h"
#include "DeviceCommand.h"
#include "DeviceFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Device, DeviceFilterFactory, L"Device")

using std::vector;
using std::wstring;

#ifndef NO_FILTERENGINE
void DeviceFilterFactory::initialize(FilterEngine* engine)
{
	deviceString = engine->getDeviceString();

	EngineParser* parser = engine->getParser();
	parser->defineConst(L"deviceName", engine->getDeviceName());
	parser->defineConst(L"connectionName", engine->getConnectionName());
	parser->defineConst(L"deviceGuid", engine->getDeviceGuid());
}
#endif

FilterVector DeviceFilterFactory::startOfConfiguration()
{
	deviceMatches = true;

	return {};
}

FilterVector DeviceFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	DeviceCommand cmd;
	if (DeviceCommand::parse(command, parameters, cmd))
	{
		bool matches = cmd.matches(deviceString);

		TraceF(L"%satching pattern \"%s\" with device \"%s\"", matches ? L"M" : L"Not m", StringHelper::trim(parameters).c_str(), deviceString.c_str());
		deviceMatches = matches;
	}

	if (!deviceMatches)
		// skip line for further factories
		command = L"";

	return {};
}

FilterVector DeviceFilterFactory::endOfFile(const std::wstring& configPath)
{
	// in outer file, the device must have matched, otherwise the inner file would not have been included
	deviceMatches = true;

	return {};
}
