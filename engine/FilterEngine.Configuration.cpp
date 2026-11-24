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


vector<wstring> FilterEngine::deviceChannelNames() const
{
	return ChannelLayout::getChannelNames(max(realChannelCount, outputChannelCount), channelMask);
}

const vector<wstring>& FilterEngine::loadingChannelNames() const
{
	return load.routing.allChannelNames();
}

bool FilterEngine::ConfigGateStack::allIfOpen() const
{
	return std::all_of(ifGates.cbegin(), ifGates.cend(), [](const IfGate& gate) {
		return gate.open;
	});
}

bool FilterEngine::ConfigGateStack::parentIfOpen() const
{
	if (ifGates.size() <= fileIfBase)
		return true;
	return std::all_of(ifGates.cbegin(), ifGates.cend() - 1, [](const IfGate& gate) {
		return gate.open;
	});
}

bool FilterEngine::ConfigGateStack::allows(ConfigGateBoundary boundary) const
{
	if (boundary == ConfigGateBoundary::None)
		return true;
	if (!deviceOpen)
		return false;
	if (boundary == ConfigGateBoundary::Device)
		return true;
	if (!allIfOpen())
		return false;
	if (boundary == ConfigGateBoundary::If)
		return true;
	return stageOpen;
}

void FilterEngine::ConfigGateStack::beginFile()
{
	fileIfBase = ifGates.size();
}

vector<int> FilterEngine::ConfigGateStack::unclosedIfLines() const
{
	vector<int> lines;
	lines.reserve(ifGates.size() - fileIfBase);
	for (size_t index = fileIfBase; index < ifGates.size(); ++index)
		lines.push_back(ifGates[index].openingLine);
	return lines;
}

void FilterEngine::setGate(ConfigGateKind kind, bool open)
{
	if (kind == ConfigGateKind::Device)
		load.gates.deviceOpen = open;
	else if (kind == ConfigGateKind::Stage)
		load.gates.stageOpen = open;
	else if (load.gates.ifGates.size() > load.gates.fileIfBase)
		load.gates.ifGates.back().open = open;
}

void FilterEngine::pushIfGate(bool open, bool branchTaken)
{
	load.gates.ifGates.push_back({open, branchTaken, load.traceLine});
}

void FilterEngine::setIfBranchTaken(bool branchTaken)
{
	if (hasIfGate())
		load.gates.ifGates.back().branchTaken = branchTaken;
}

void FilterEngine::popIfGate()
{
	if (hasIfGate())
		load.gates.ifGates.pop_back();
}

bool FilterEngine::hasIfGate() const
{
	return load.gates.ifGates.size() > load.gates.fileIfBase;
}

bool FilterEngine::ifGatesOpen() const
{
	return load.gates.allIfOpen();
}

bool FilterEngine::ifGateParentOpen() const
{
	return load.gates.parentIfOpen();
}

bool FilterEngine::ifBranchTaken() const
{
	return hasIfGate() && load.gates.ifGates.back().branchTaken;
}

void FilterEngine::deferUntilConfigurationCommit(std::function<void()> action)
{
	load.commitActions.push_back(std::move(action));
}

void FilterEngine::reportHostBlockTiming(const wchar_t* when)
{
	const eapo::diag::BlockTimingSummary summary = hostBlockTiming.take();
	if (summary.blocks == 0)
		return;

	const double millisecondsPerTick = 1000.0 / static_cast<double>(performanceFrequency);
	const wchar_t* const format =
		L"Audio blocks %s: %llu callbacks of %.2f ms; %llu took longer than the audio they carry "
		L"(longest %.2f ms, for %.2f ms of audio); %llu began more than 1.5 block lengths after "
		L"the previous one (longest gap %.2f ms)";
	if (summary.overBudget > 0 || summary.lateStarts > 0)
	{
		LogF(format, when, static_cast<unsigned long long>(summary.blocks),
			summary.lastBudgetTicks * millisecondsPerTick,
			static_cast<unsigned long long>(summary.overBudget),
			summary.longestTicks * millisecondsPerTick,
			summary.longestBudgetTicks * millisecondsPerTick,
			static_cast<unsigned long long>(summary.lateStarts),
			summary.longestGapTicks * millisecondsPerTick);
	}
	else
	{
		TraceF(format, when, static_cast<unsigned long long>(summary.blocks),
			summary.lastBudgetTicks * millisecondsPerTick,
			static_cast<unsigned long long>(summary.overBudget),
			summary.longestTicks * millisecondsPerTick,
			summary.longestBudgetTicks * millisecondsPerTick,
			static_cast<unsigned long long>(summary.lateStarts),
			summary.longestGapTicks * millisecondsPerTick);
	}
}

bool FilterEngine::loadConfig(const wstring& customPath)
{
	// One producer may own the pending slot until the audio thread completes
	// its transition. Keeping the permit here makes every caller obey the same
	// publication rule and prevents a later load from replacing an unconsumed
	// configuration.
	if (!configChannel.acquirePublishPermit())
		return false;

	lock_guard<mutex> lock(loadMutex);
	const uint64_t observedOversizedBlockCount = oversizedBlockCount.load(std::memory_order_relaxed);
	if (observedOversizedBlockCount != reportedOversizedBlockCount)
	{
		LogF(L"host passed %u frames, more than the %u it announced; block passed through unprocessed",
			lastOversizedFrameCount.load(std::memory_order_relaxed),
			lastOversizedMaxFrameCount.load(std::memory_order_relaxed));
		reportedOversizedBlockCount = observedOversizedBlockCount;
	}
	reportHostBlockTiming(L"since the previous configuration load");
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
		load.routing.begin(deviceChannelNames(), saved.routing.lastInPlace());
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

		// Construction is complete and no later load operation can roll back. Run
		// deferred side effects immediately before publication, so Send metadata is
		// ready before the audio thread can acquire this configuration and never
		// describes a load that unwound (audit #467 TD-45).
		for (const std::function<void()>& action : load.commitActions)
		{
			try
			{
				action();
			}
			catch (const exception& e)
			{
				LogF(L"A configuration commit action failed: %S", e.what());
			}
			catch (...)
			{
				LogF(L"A configuration commit action failed with an unknown exception");
			}
		}
		load.commitActions.clear();

		configChannel.publish(move(config));
		// The pending configuration now owns the permit. completeTransition()
		// returns it on the audio thread after promoting the pending slot.
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
		configChannel.releasePublishPermit();
		LogF(L"Configuration load failed; keeping the active configuration: %S%s", e.what(), where.c_str());
	}
	catch (...)
	{
		rollback();
		timer.stop();
		configChannel.releasePublishPermit();
		LogF(L"Configuration load failed with an unknown exception; keeping the active configuration");
	}
	return false;
}

void FilterEngine::loadConfigFile(const wstring& path)
{
	TraceF(L"Loading configuration from %s", path.c_str());

	loadConfigStream(path, ConfigurationFileReader::readWithRetry(path, configChannel.shutdownHandle()));
}

ConfigFileReference::Target FilterEngine::judgeIncludedFile(const wstring& configPath, const wstring& written)
{
	return ConfigurationFileReader::judgeWithRetry(configPath, written, configChannel.shutdownHandle());
}

void FilterEngine::loadConfigFile(JudgedPath path)
{
	stringstream bytes = ConfigurationFileReader::read(path);
	const wstring name = path.path();
	// The handles kept the judged file and its folders in place until the
	// bytes were read; nothing reads the file by name afterwards. Held through
	// the parse below, they stayed open while every filter in the file was set
	// up, which for a VST plug-in or a long impulse response takes seconds,
	// and the leaf handle shares no delete access: the Editor's atomic save of
	// that file (a rename over it) failed with access denied whenever the save
	// itself had woken this reload (issue #477). config.txt was never affected
	// because its reader closes the file before parsing.
	path = JudgedPath{};
	loadConfigStream(name, move(bytes));
}

void FilterEngine::loadConfigStream(const wstring& path, stringstream inputStream)
{
	if (!inputStream.good())
		return;

	vector<wstring> savedChannelNames = load.routing.currentChannelNames();
	ConfigGateStack savedGates = load.gates;
	// Load-trace position, selected channels and gates are one Include frame.
	// Restore all three even when setup in the nested file throws.
	wstring savedTraceFile = move(load.traceFile);
	const int savedTraceLine = load.traceLine;
	load.traceFile = path;
	load.traceLine = 0;
	load.gates.beginFile();

	auto restoreIncludeFrame = [&]() {
		load.routing.setCurrentChannelNames(move(savedChannelNames));
		load.gates = move(savedGates);
		load.traceFile = move(savedTraceFile);
		load.traceLine = savedTraceLine;
	};

	try
	{
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

			const size_t pos = line.find(L':');
			if (pos == wstring::npos)
				continue;

			wstring key = text::trim(line.substr(0, pos));
			wstring value = line.substr(pos + 1);

			// Factories may normalize parameters in place before returning NotMine;
			// later factories deliberately receive that rewritten text. The engine
			// alone enforces gates at the same boundaries as the historical factory
			// order and records every executable line it stops (audit #467 TD-45).
			for (auto it = factories.cbegin(); it != factories.cend(); it++)
			{
				IFilterFactory* factory = it->get();
				if (!load.gates.allows(factory->gateBoundary()))
				{
					if (!key.empty() && key[0] != L'#')
					{
						ConfigLoadTraceEntry entry;
						entry.kind = ConfigLoadTraceEntry::Kind::SkippedLine;
						traceLoadEvent(std::move(entry));
					}
					break;
				}

				LineOutcome outcome = LineOutcome::notMine();
				try
				{
					outcome = factory->createFilter(path, key, value);
				}
				catch (const exception& e)
				{
					// Stamped like reportParseError's log line, so an exception
					// escaping a factory is as locatable as a parse error
					// (audit #275 TD-03).
					LogF(L"%S (line %d of %s)", e.what(), load.traceLine, load.traceFile.c_str());
				}

				if (outcome.kind == LineOutcome::Kind::NotMine)
					continue;
				if (outcome.kind == LineOutcome::Kind::Malformed)
					reportParseError(key, outcome.reason);
				else if (outcome.kind == LineOutcome::Kind::Produced)
					addFilters(move(outcome.filters));
				break;
			}
		}

		for (auto it = factories.cbegin(); it != factories.cend(); it++)
		{
			IFilterFactory* factory = it->get();
			FilterVector newFilters = factory->endOfFile(path);
			if (!newFilters.empty())
				addFilters(move(newFilters));
		}

		for (const int line : load.gates.unclosedIfLines())
			reportParseError(L"If", L"this If has no EndIf before the end of its file", line);
	}
	catch (...)
	{
		restoreIncludeFrame();
		throw;
	}

	restoreIncludeFrame();
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

void FilterEngine::watchEvent(const std::wstring& name)
{
	load.watchEvents.insert(name);
}
