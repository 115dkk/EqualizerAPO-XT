/*
	This file is part of EqualizerAPO-XT.
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

	bool fileExists(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}
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
	currentSynchronous = false;
	WrapperRecord record;
	if (eapo::asio::AsioRegistration::wrapperRegistered(registry, target)
		&& WrapperRecords::read(registry, wrapperClsidFor(target.clsid), record))
	{
		installed = input ? record.options.processInput : record.options.processOutput;
		currentSynchronous = record.options.mode == eapo::asio::Mode::Sync;
	}
	selectedSynchronous = currentSynchronous;

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
	return installed && selectedSynchronous != currentSynchronous;
}

bool AsioAPOInfo::isExperimental() const
{
	return false;
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

void AsioAPOInfo::install()
{
	const std::wstring wrapperClsid = wrapperClsidFor(target.clsid);
	WrapperRecord record;
	if (!WrapperRecords::read(registry, wrapperClsid, record))
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
	record.options.mode = selectedSynchronous ? eapo::asio::Mode::Sync : eapo::asio::Mode::Pipelined;
	WrapperRecords::write(registry, record);

	const std::wstring directory = installDirectory();
	const std::wstring dll64 = directory + L"\\EqualizerAPOAsio.dll";
	// The 32-bit wrapper ships beside the 64-bit one under x86\; a build
	// without it registers the 64-bit view only.
	const std::wstring dll32 = directory + L"\\x86\\EqualizerAPOAsio.dll";
	eapo::asio::AsioRegistration::registerWrapper(registry, target, dll64, fileExists(dll32) ? dll32 : std::wstring());
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
			loadState();
			return;
		}
		WrapperRecords::remove(registry, wrapperClsid);
	}
	eapo::asio::AsioRegistration::unregisterWrapper(registry, target);
	loadState();
}

void AsioAPOInfo::reinstall()
{
	const bool synchronous = selectedSynchronous;
	uninstall();
	selectedSynchronous = synchronous;
	install();
}
