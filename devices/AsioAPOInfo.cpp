/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "devices/AsioAPOInfo.h"
#include "devices/AsioEntryWriter.h"

#include "asio/StreamFacts.h"
#include "asio/WrapperRecord.h"
#include "audio/ChannelLayout.h"
#include "devices/DeviceException.h"
#include "devices/ReportedOperation.h"
#include "services/registry/RegistryTransaction.h"
#include "services/registry/RegistryError.h"
#include "services/registry/RegistryPaths.h"

using eapo::asio::AsioRegistration::entryNameFor;
using eapo::asio::AsioRegistration::wrapperClsidFor;
using eapo::asio::AsioTarget;
using eapo::asio::WrapperRecord;
namespace WrapperRecords = eapo::asio::WrapperRecords;

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

	// What the engine host saw of the target's last stream; nothing until a
	// DAW has opened it once.
	eapo::asio::StreamShape shape;
	eapo::asio::StreamFacts::read(registry, target.clsid, shape);
	const eapo::asio::Direction direction = input ? eapo::asio::Direction::Input : eapo::asio::Direction::Output;
	channelCount = shape.channels[static_cast<unsigned>(direction)];
	sampleRate = shape.sampleRate;
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

bool AsioAPOInfo::hasChanges() const
{
	return installed && selected != current;
}

std::wstring AsioAPOInfo::getTransportLabel() const
{
	return L"ASIO";
}

bool AsioAPOInfo::canHost32() const
{
	// A build without the x86 wrapper (ARM64) cannot serve 32-bit hosts.
	return eapo::asio::AsioRegistration::wrapper32Shipped(AsioEntryWriter::installDirectory(registry, AsioEntryWriter::DirectoryRequirement::Optional));
}

void AsioAPOInfo::beginReport(DeviceInstallReport::Operation operation)
{
	DeviceInstallReport report;
	report.operation = operation;
	report.deviceName = target.name;
	report.connectionName = getConnectionName();
	report.deviceGuid = target.clsid;
	report.input = input;
	if (operation != DeviceInstallReport::Operation::Uninstall)
		report.asioEntry = entryNameFor(target.name);
	lastOperationReport = report;
}

void AsioAPOInfo::installWithin(RegistryTransaction& plan)
{
	const std::wstring directory = AsioEntryWriter::installDirectory(plan, AsioEntryWriter::DirectoryRequirement::Required);
	const std::wstring wrapperClsid = wrapperClsidFor(target.clsid);
	WrapperRecord record;
	const bool fresh = !WrapperRecords::read(plan, wrapperClsid, record);
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
	AsioEntryWriter::writeEntry(plan, target, record, options, directory);
}

void AsioAPOInfo::uninstallWithin(RegistryTransaction& plan)
{
	// Removing needs no install directory: the Run value only has to be
	// rewritten when another entry still asks for it, and then it is kept.
	const std::wstring directory = AsioEntryWriter::installDirectory(plan, AsioEntryWriter::DirectoryRequirement::Optional);
	const std::wstring wrapperClsid = wrapperClsidFor(target.clsid);
	WrapperRecord record;
	if (WrapperRecords::read(plan, wrapperClsid, record))
	{
		if (input)
			record.options.processInput = false;
		else
			record.options.processOutput = false;
	}
	else
	{
		record.wrapperClsid = wrapperClsid;
		record.options.processInput = false;
		record.options.processOutput = false;
	}
	AsioEntryWriter::removeEntry(plan, target, record, directory);
}

void AsioAPOInfo::install()
{
	beginReport(DeviceInstallReport::Operation::Install);
	ReportedOperation::run(registry, lastOperationReport, [this](RegistryTransaction& plan) {
		installWithin(plan);
	});
	loadState();
}

void AsioAPOInfo::uninstall()
{
	beginReport(DeviceInstallReport::Operation::Uninstall);
	ReportedOperation::run(registry, lastOperationReport, [this](RegistryTransaction& plan) {
		uninstallWithin(plan);
	});
	loadState();
}

void AsioAPOInfo::reinstall()
{
	// One transaction for both halves: a failing install puts the entry back
	// as it was instead of leaving it removed (the endpoint adapter's
	// reinstall had the same fix, see RegistryTransaction.h).
	beginReport(DeviceInstallReport::Operation::Reinstall);
	ReportedOperation::run(registry, lastOperationReport, [this](RegistryTransaction& plan) {
		uninstallWithin(plan);
		installWithin(plan);
	});
	loadState();
}
