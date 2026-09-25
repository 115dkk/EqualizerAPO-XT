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
#include "text/WideString.h"
#include <cmath>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <exception>
#include <set>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "services/registry/WindowsRegistry.h"
#include "services/logging/Logging.h"
#include "runtime/memory/AlignedMemory.h"
#include "audio/ChannelLayout.h"
#include "ConfigLoadTrace.h"
#include "ConfigurationFileReader.h"
#include "platform/windows/TextEncoding.h"
#include "FilterEngine.h"
// The individual filter factories self-register via REGISTER_FILTER_FACTORY, and
// every consumer links Common.lib with /WHOLEARCHIVE, which forces each factory
// translation unit into the link without the engine naming or including it. So
// only the registry facade is needed here, not the 15 factory headers.
#include "filters/FilterFactoryRegistry.h"

using std::exception;
using std::lock_guard;
using std::make_unique;
using std::max;
using std::move;
using std::mutex;
using std::stringstream;
using std::thread;
using std::vector;
using std::wstring;


bool FilterEngine::loadConfig(const wstring& customPath)
{
	lock_guard<mutex> lock(loadMutex);
	timer.start();

	// The factories build through the engine's load session. Move the previous
	// idle session aside so the whole load is transactional: any exception
	// discards the partial filters/channel routing and restores registry
	// watches, while the active configuration remains untouched. Audit #250
	// A1: the transaction is a move of one value - a field added to
	// LoadSession is covered by construction, instead of by keeping a save
	// block, a rollback lambda and the member list in step by hand.
	LoadSession saved = move(load);
	load = LoadSession{};

	auto rollback = [&]() noexcept {
		load = move(saved);
	};

	try
	{
		// The in-place-ness of the previous load's last filter deliberately
		// carries across loads: the first filter's output-inheritance test in
		// addFilters reads it (see the channel-inheritance contract in
		// FilterConfiguration.h).
		load.routing.begin(ChannelLayout::getChannelNames(max(realChannelCount, outputChannelCount), channelMask),
			saved.routing.lastInPlace());
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

		FilterConfigurationPtr config(AlignedMemory::construct<FilterConfiguration>(streamFormat(), move(load.filterInfos), (unsigned)load.routing.allChannelNames().size()));

		load.filterInfos.clear();

		double loadTime = timer.stop();
		TraceF(L"Finished loading configuration after %lf milliseconds", loadTime * 1000.0);

		configChannel.publish(move(config));
		// Release: publish the fully-constructed FilterConfiguration to the RT
		// thread. Pairs with the acquire loads in process()/finishTransitionIfReady.
		return true;
	}
	catch (const exception& e)
	{
		// An exception out of a line leaves the load positioned on it, since
		// loadConfigFile restores the position only on its way out normally.
		const wstring where = load.traceLine > 0
			? L" (line " + std::to_wstring(load.traceLine) + L" of " + load.traceFile + L")" : wstring();
		rollback();
		timer.stop();
		LogF(L"Configuration load failed; keeping the active configuration: %S%s", e.what(), where.c_str());
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

	stringstream inputStream = ConfigurationFileReader::readWithRetry(path, configChannel.shutdownHandle());
	if (!inputStream.good())
		return;

	vector<wstring> savedChannelNames = load.routing.currentChannelNames();
	// Load-trace position: like the channel names, the position is saved and
	// restored across the Include recursion so entries reported after a nested
	// file returns are stamped with the outer file again.
	wstring savedTraceFile = move(load.traceFile);
	int savedTraceLine = load.traceLine;
	load.traceFile = path;
	load.traceLine = 0;

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
		load.traceLine++;

		size_t pos = line.find(L':');
		if (pos != wstring::npos)
		{
			wstring key = line.substr(0, pos);
			wstring value = line.substr(pos + 1);

			// allow to use indentation
			key = text::trim(key);

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
					// Stamped like reportParseError's log line, so an exception
					// escaping a factory is as locatable as a parse error
					// (audit #275 TD-03).
					LogF(L"%S (line %d of %s)", e.what(), load.traceLine, load.traceFile.c_str());
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
	load.routing.setCurrentChannelNames(move(savedChannelNames));
	load.traceFile = move(savedTraceFile);
	load.traceLine = savedTraceLine;
}

// Load-time graph construction: assigns each new filter its channel index
// mapping against the growing all-channel list. Lives here with the rest of
// the loading code (moved from FilterEngine.Runtime.cpp, audit #275 A6).
void FilterEngine::addFilters(FilterVector filters)
{
	for (FilterPtr& ownedFilter : filters)
	{
		auto filterInfo = make_unique<FilterInfo>();
		filterInfo->filter = move(ownedFilter);
		IFilter* filter = filterInfo->filter.get();
		filterInfo->inPlace = filter->getInPlace();
		ChannelRoutingPlan::Entry entry = load.routing.enter(filter->getAllChannels());
		filterInfo->inChannels = move(entry.inChannels);

		vector<wstring> newChannelNames;
		try
		{
			newChannelNames = filter->initialize(sampleRate, maxFrameCount, move(entry.initializeWith));
		}
		catch (const exception& e)
		{
			// The load still rolls back as a whole (audit #348 TD-18), but the
			// line whose filter failed goes on the load trace first, so the
			// Editor can point at it; loadConfig's log line names it too.
			ConfigLoadTraceEntry traceEntry;
			traceEntry.kind = ConfigLoadTraceEntry::Kind::SetupError;
			traceEntry.error = true;
			traceEntry.text = L"could not be set up (" + wintext::toWideString(e.what(), CP_UTF8)
				+ L"), so the configuration was not applied";
			traceLoadEvent(std::move(traceEntry));
			throw;
		}

		filterInfo->outChannels = load.routing.leave(newChannelNames, filterInfo->inPlace, filter->getSelectChannels());

		load.filterInfos.push_back(move(filterInfo));
	}
}

void FilterEngine::reportParseError(const wstring& command, const wstring& reason, int line)
{
	const int currentLine = load.traceLine;
	if (line > 0)
		load.traceLine = line;
	// The log line goes out whether or not a sink is attached: the APO runtime
	// never attaches one, and a user whose Convolution line silently does nothing
	// has to be able to find out why from the log.
	LogF(L"%s: %s (line %d of %s)", command.c_str(), reason.c_str(), load.traceLine, load.traceFile.c_str());

	ConfigLoadTraceEntry entry;
	entry.kind = ConfigLoadTraceEntry::Kind::ParseError;
	entry.error = true;
	entry.text = reason;
	traceLoadEvent(std::move(entry));
	load.traceLine = currentLine;
}

void FilterEngine::traceLoadEvent(ConfigLoadTraceEntry entry)
{
	if (traceSink == nullptr)
		return;
	entry.file = load.traceFile;
	entry.line = load.traceLine;
	traceSink->addEntry(entry);
}

void FilterEngine::watchRegistryKey(const std::wstring& key)
{
	load.watchRegistryKeys.insert(key);
}
