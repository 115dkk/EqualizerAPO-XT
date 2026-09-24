/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "devices/AsioAPOInfo.h"

#include "asio/WrapperRecord.h"
#include "audio/ChannelLayout.h"
#include "services/registry/RegistryError.h"
#include "services/registry/RegistryPaths.h"

using eapo::asio::AsioRegistration::entryNameFor;
using eapo::asio::AsioRegistration::wrapperClsidFor;
using eapo::asio::AsioTarget;
using eapo::asio::WrapperRecord;
namespace WrapperRecords = eapo::asio::WrapperRecords;

namespace
{
	const wchar_t* const sampleRateFact = L"SampleRate";
	const wchar_t* const outputChannelsFact = L"OutputChannels";
	const wchar_t* const inputChannelsFact = L"InputChannels";
}

void AsioAPOInfo::appendInfos(std::vector<std::shared_ptr<AbstractAPOInfo>>& list, bool input, IRegistry& registry)
{
	for (const AsioTarget& target : eapo::asio::AsioRegistration::enumerateTargets(registry))
		list.push_back(std::make_shared<AsioAPOInfo>(target, input, registry));
}

AsioAPOInfo::AsioAPOInfo(const AsioTarget& target, bool input, IRegistry& registry)
	: target(target), input(input), registry(registry)
{
	loadState();
}

std::wstring AsioAPOInfo::factsKey(const std::wstring& targetClsid)
{
	return std::wstring(USER_REGPATH) + L"\\ASIO\\" + targetClsid;
}

void AsioAPOInfo::loadState()
{
	installed = false;
	current = {};
	WrapperRecord record;
	if (eapo::asio::AsioRegistration::wrapperRegistered(registry, target)
		&& WrapperRecords::read(registry, wrapperClsidFor(target.clsid), record))
	{
		installed = input ? record.options.processInput : record.options.processOutput;
		current = WrapperRecords::entryOptions(record);
	}
	selected = current;

	channelCount = 0;
	sampleRate = 0;
	const std::wstring facts = factsKey(target.clsid);
	if (registry.keyExists(facts))
	{
		const wchar_t* const channelsFact = input ? inputChannelsFact : outputChannelsFact;
		if (registry.valueExists(facts, channelsFact))
			channelCount = registry.readDWORDValue(facts, channelsFact);
		if (registry.valueExists(facts, sampleRateFact))
			sampleRate = registry.readDWORDValue(facts, sampleRateFact);
	}
}

std::wstring AsioAPOInfo::getWrapperClsid() const
{
	return wrapperClsidFor(target.clsid);
}

std::wstring AsioAPOInfo::getConnectionName() const
{
	return L"ASIO";
}

std::wstring AsioAPOInfo::getDeviceName() const
{
	return target.name;
}

std::wstring AsioAPOInfo::getDeviceGuid() const
{
	return target.clsid;
}

std::wstring AsioAPOInfo::getDeviceString() const
{
	// What the engine matches Device: lines against: the same three parts
	// the host puts into EngineSetup for this target.
	return getConnectionName() + L" " + getDeviceName() + L" " + getDeviceGuid();
}

unsigned AsioAPOInfo::getChannelCount() const
{
	return channelCount;
}

unsigned AsioAPOInfo::getSampleRate() const
{
	return sampleRate;
}

unsigned long AsioAPOInfo::getChannelMask() const
{
	return channelCount == 0 ? 0 : ChannelLayout::getDefaultChannelMask(static_cast<int>(channelCount));
}

bool AsioAPOInfo::isInput() const
{
	return input;
}

bool AsioAPOInfo::isInstalled() const
{
	return installed;
}

bool AsioAPOInfo::canBeUpgraded() const
{
	return false;
}

bool AsioAPOInfo::hasChanges() const
{
	return installed && selected != current;
}

bool AsioAPOInfo::isEnhancementsDisabled() const
{
	return false;
}

bool AsioAPOInfo::isDefaultDevice() const
{
	// Decision 3: no group of its own and no default of its own.
	return false;
}

bool AsioAPOInfo::isDisabled() const
{
	return false;
}

bool AsioAPOInfo::isUnplugged() const
{
	return false;
}

std::wstring AsioAPOInfo::getTransportLabel() const
{
	return L"ASIO";
}

std::wstring AsioAPOInfo::installDirectory() const
{
	return registry.readValue(APP_REGPATH, L"InstallPath");
}

bool AsioAPOInfo::canHost32() const
{
	// A build without the x86 wrapper (ARM64) cannot serve 32-bit hosts.
	return eapo::asio::AsioRegistration::wrapper32Shipped(installDirectory());
}

void AsioAPOInfo::install()
{
	const std::wstring wrapperClsid = wrapperClsidFor(target.clsid);
	WrapperRecord record;
	const bool fresh = !WrapperRecords::read(registry, wrapperClsid, record);
	if (fresh)
	{
		record.wrapperClsid = wrapperClsid;
		record.targetClsid = target.clsid;
		record.targetName = target.name;
		record.options.processOutput = false;
		record.options.processInput = false;
	}
	if (input)
		record.options.processInput = true;
	else
		record.options.processOutput = true;
	// Options this row changed win; the other direction's row, installed in
	// the same pass with an untouched selection, must not put them back.
	eapo::asio::EntryOptions options = fresh ? selected : WrapperRecords::entryOptions(record);
	if (selected.synchronous != current.synchronous)
		options.synchronous = selected.synchronous;
	if (selected.deadlinePercent != current.deadlinePercent)
		options.deadlinePercent = selected.deadlinePercent;
	if (selected.autoStart != current.autoStart)
		options.autoStart = selected.autoStart;
	if (selected.host32 != current.host32)
		options.host32 = selected.host32;
	WrapperRecords::setEntryOptions(record, options);
	WrapperRecords::write(registry, record);

	// The 32-bit view only when asked for, and only when the x86 wrapper
	// is there to point at.
	const std::wstring directory = installDirectory();
	eapo::asio::AsioRegistration::registerWrapper(registry, target,
		eapo::asio::AsioRegistration::wrapperDllPath(directory),
		record.register32 && eapo::asio::AsioRegistration::wrapper32Shipped(directory)
			? eapo::asio::AsioRegistration::wrapper32DllPath(directory) : std::wstring());
	eapo::asio::AsioRegistration::refreshAutoStart(registry, directory);
	loadState();
}

void AsioAPOInfo::uninstall()
{
	const std::wstring wrapperClsid = wrapperClsidFor(target.clsid);
	WrapperRecord record;
	if (WrapperRecords::read(registry, wrapperClsid, record))
	{
		if (input)
			record.options.processInput = false;
		else
			record.options.processOutput = false;
		if (record.options.processInput || record.options.processOutput)
		{
			WrapperRecords::write(registry, record);
			eapo::asio::AsioRegistration::refreshAutoStart(registry, installDirectory());
			loadState();
			return;
		}
		WrapperRecords::remove(registry, wrapperClsid);
	}
	eapo::asio::AsioRegistration::unregisterWrapper(registry, target);
	eapo::asio::AsioRegistration::refreshAutoStart(registry, installDirectory());
	loadState();
}

void AsioAPOInfo::reinstall()
{
	const eapo::asio::EntryOptions options = selected;
	uninstall();
	selected = options;
	install();
}
