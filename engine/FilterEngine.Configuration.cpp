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
#define _USE_MATH_DEFINES
#include <cmath>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <exception>
#include <set>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mpParser.h>

#include "helpers/RegistryHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/ChannelHelper.h"
#include "ConfigLoadTrace.h"
#include "ConfigurationFileReader.h"
#include "FilterEngine.h"
// The individual filter factories self-register via REGISTER_FILTER_FACTORY, and
// every consumer links Common.lib with /WHOLEARCHIVE, which forces each factory
// translation unit into the link without the engine naming or including it. So
// only the registry facade is needed here, not the 15 factory headers.
#include "filters/FilterFactoryRegistry.h"

using std::exception;
using std::find;
using std::lock_guard;
using std::make_unique;
using std::max;
using std::move;
using std::mutex;
using std::string;
using std::stringstream;
using std::swap;
using std::thread;
using std::unique_lock;
using std::vector;
using std::wstring;
using namespace mup;


bool FilterEngine::loadConfig(const wstring& customPath)
{
	lock_guard<mutex> lock(loadMutex);
	timer.start();

	// The factories build through these engine-owned scratch fields. Move the
	// previous idle state aside so the whole load is transactional: any exception
	// discards the partial filters/channel routing and restores registry watches,
	// while the active configuration remains untouched.
	auto savedFilterInfos = move(filterInfos);
	auto savedCurrentChannelNames = move(currentChannelNames);
	auto savedLastChannelNames = move(lastChannelNames);
	auto savedLastNewChannelNames = move(lastNewChannelNames);
	auto savedAllChannelNames = move(allChannelNames);
	auto savedWatchRegistryKeys = move(watchRegistryKeys);
	auto savedTraceFile = move(traceFile);
	const int savedTraceLine = traceLine;
	const bool savedLastInPlace = lastInPlace;
	const bool savedFrozenDynamicAnalysis = frozenDynamicAnalysis;
	frozenDynamicAnalysis = false;

	auto rollback = [&]() noexcept {
		filterInfos = move(savedFilterInfos);
		currentChannelNames = move(savedCurrentChannelNames);
		lastChannelNames = move(savedLastChannelNames);
		lastNewChannelNames = move(savedLastNewChannelNames);
		allChannelNames = move(savedAllChannelNames);
		watchRegistryKeys = move(savedWatchRegistryKeys);
		traceFile = move(savedTraceFile);
		traceLine = savedTraceLine;
		lastInPlace = savedLastInPlace;
		frozenDynamicAnalysis = savedFrozenDynamicAnalysis;
	};

	try
	{
		allChannelNames = ChannelHelper::getChannelNames(max(realChannelCount, outputChannelCount), channelMask);

		currentChannelNames = allChannelNames;
		lastChannelNames.clear();
		lastNewChannelNames.clear();
		watchRegistryKeys.clear();
		parser.beginLoad();

		for (auto it = factories.cbegin(); it != factories.cend(); it++)
		{
			IFilterFactory* factory = it->get();
			FilterVector newFilters = factory->startOfConfiguration();
			if (!newFilters.empty())
				addFilters(move(newFilters));
		}

		if (customPath.empty())
			loadConfigFile(configPath + L"\\config.txt");
		else
			loadConfigFile(customPath);

		for (auto it = factories.cbegin(); it != factories.cend(); it++)
		{
			IFilterFactory* factory = it->get();
			FilterVector newFilters = factory->endOfConfiguration();
			if (!newFilters.empty())
				addFilters(move(newFilters));
		}

		FilterConfigurationPtr config(MemoryHelper::construct<FilterConfiguration>(streamFormat(), move(filterInfos), (unsigned)allChannelNames.size()));

		filterInfos.clear();

		double loadTime = timer.stop();
		TraceF(L"Finished loading configuration after %lf milliseconds", loadTime * 1000.0);

		configChannel.publish(move(config));
		// Release: publish the fully-constructed FilterConfiguration to the RT
		// thread. Pairs with the acquire loads in process()/finishTransitionIfReady.
		return true;
	}
	catch (const exception& e)
	{
		rollback();
		timer.stop();
		LogF(L"Configuration load failed; keeping the active configuration: %S", e.what());
	}
	catch (...)
	{
		rollback();
		timer.stop();
		LogF(L"Configuration load failed with an unknown exception; keeping the active configuration");
	}
	return false;
}

void FilterEngine::loadConfigFile(const wstring& path)
{
	TraceF(L"Loading configuration from %s", path.c_str());

	stringstream inputStream = ConfigurationFileReader::readWithRetry(path);
	if (!inputStream.good())
		return;

	vector<wstring> savedChannelNames = currentChannelNames;
	// Load-trace position: like the channel names, the position is saved and
	// restored across the Include recursion so entries reported after a nested
	// file returns are stamped with the outer file again.
	wstring savedTraceFile = move(traceFile);
	int savedTraceLine = traceLine;
	traceFile = path;
	traceLine = 0;

	for (auto it = factories.cbegin(); it != factories.cend(); it++)
	{
		IFilterFactory* factory = it->get();
		FilterVector newFilters = factory->startOfFile(path);
		if (!newFilters.empty())
			addFilters(move(newFilters));
	}

	const vector<wstring> decodedLines = ConfigurationFileReader::decodeLines(inputStream);
	for (const wstring& line : decodedLines)
	{
		traceLine++;

		size_t pos = line.find(L':');
		if (pos != wstring::npos)
		{
			wstring key = line.substr(0, pos);
			wstring value = line.substr(pos + 1);

			// allow to use indentation
			key = StringHelper::trim(key);

			// No verdict is reached here about a line that produced nothing. A
			// factory that recognised the command and could not use it says so
			// itself, through reportParseError, at the point where it knows what
			// was wrong. What is left over here is a line no factory claimed:
			// prose, a comment, a note, an unknown key - and that is not an error.
			for (auto it = factories.cbegin(); it != factories.cend(); it++)
			{
				IFilterFactory* factory = it->get();

				FilterVector newFilters;
				try
				{
					newFilters = factory->createFilter(path, key, value);
				}
				catch (const exception& e)
				{
					LogF(L"%S", e.what());
				}

				if (key == L"")
					break;
				if (!newFilters.empty())
				{
					addFilters(move(newFilters));
					break;
				}
			}
		}
	}

	for (auto it = factories.cbegin(); it != factories.cend(); it++)
	{
		IFilterFactory* factory = it->get();
		FilterVector newFilters = factory->endOfFile(path);
		if (!newFilters.empty())
			addFilters(move(newFilters));
	}

	// restore channels selected in outer configuration file
	currentChannelNames = savedChannelNames;
	traceFile = move(savedTraceFile);
	traceLine = savedTraceLine;
}

void FilterEngine::reportParseError(const wstring& command, const wstring& reason)
{
	// The log line goes out whether or not a sink is attached: the APO runtime
	// never attaches one, and a user whose Convolution line silently does nothing
	// has to be able to find out why from the log.
	LogF(L"%s: %s (line %d of %s)", command.c_str(), reason.c_str(), traceLine, traceFile.c_str());

	ConfigLoadTraceEntry entry;
	entry.kind = ConfigLoadTraceEntry::Kind::ParseError;
	entry.error = true;
	entry.text = reason;
	traceLoadEvent(std::move(entry));
}

void FilterEngine::traceLoadEvent(ConfigLoadTraceEntry entry)
{
	if (traceSink == nullptr)
		return;
	entry.file = traceFile;
	entry.line = traceLine;
	traceSink->addEntry(entry);
}

void FilterEngine::watchRegistryKey(const std::wstring& key)
{
	watchRegistryKeys.insert(key);
}
