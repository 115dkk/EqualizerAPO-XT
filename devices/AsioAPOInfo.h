/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The device record for an ASIO target driver: the fourth adapter of
	AbstractAPOInfo, next to the MMDevice endpoint, the Voicemeeter bus and
	the gallery preview. One target driver yields two records, a playback
	one and a capture one, the way a USB interface shows up as a render and
	a capture endpoint; the DeviceSelector lists them in its two groups with
	a one-word transport label and nothing else different (decision 3 in
	docs/architecture/asio-host-study.md, section 9).

	"Installed" means the wrapper entry exists and the wrapper record enables
	this record's direction. install() and uninstall() therefore edit the
	record's ProcessOutput/ProcessInput flag; the first direction installed
	registers the wrapper entry in both registry views, the last one removed
	unregisters it. Channel count and sample rate come from the facts the
	engine host publishes under HKCU once a stream has run, and are zero
	before that.
*/

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "asio/AsioRegistration.h"
#include "asio/EntryOptions.h"
#include "devices/AbstractAPOInfo.h"
#include "services/registry/IRegistry.h"

class RegistryTransaction;

class AsioAPOInfo : public AbstractAPOInfo
{
public:
	// Appends one record per target driver for the given direction. Called
	// from DeviceAPOInfo::loadAllInfos, the one place the device kinds are
	// assembled, with the registry it was given.
	static void appendInfos(std::vector<std::shared_ptr<AbstractAPOInfo>>& list, bool input, IRegistry& registry = systemRegistry());

	AsioAPOInfo(const eapo::asio::AsioTarget& target, bool input, IRegistry& registry = systemRegistry());

	std::wstring getConnectionName() const override;
	std::wstring getDeviceName() const override;
	std::wstring getDeviceGuid() const override;
	std::wstring getDeviceString() const override;
	unsigned getChannelCount() const override;
	unsigned getSampleRate() const override;
	unsigned long getChannelMask() const override;
	bool isInput() const override;
	bool isInstalled() const override;
	bool canBeUpgraded() const override;
	bool hasChanges() const override;
	bool isEnhancementsDisabled() const override;
	bool isDefaultDevice() const override;
	bool isDisabled() const override;
	bool isUnplugged() const override;
	std::wstring getTransportLabel() const override;
	void install() override;
	void uninstall() override;
	void reinstall() override;

	const eapo::asio::AsioTarget& getTarget() const {return target;}
	std::wstring getWrapperClsid() const;

	// The Device Selector's options for this target (asio/EntryOptions.h).
	// Both directions share them, since they share the wrapper record.
	const eapo::asio::EntryOptions& getEntryOptions() const {return selected;}
	void setEntryOptions(const eapo::asio::EntryOptions& options) {selected = options;}

	// The synchronous mode (no extra buffer; a missed deadline passes the
	// buffer through).
	bool isSynchronous() const {return selected.synchronous;}
	void setSynchronous(bool synchronous) {selected.synchronous = synchronous;}

	// How much of the buffer period a synchronous buffer waits for the host
	// before it passes through: 25 (the default), 50 or 75.
	unsigned getDeadlinePercent() const {return selected.deadlinePercent;}
	void setDeadlinePercent(unsigned percent) {selected.deadlinePercent = percent;}

	// Start the engine host at boot: one Run value shared by every entry,
	// kept while any installed entry asks for it. Off by default.
	bool isAutoStart() const {return selected.autoStart;}
	void setAutoStart(bool autoStart) {selected.autoStart = autoStart;}

	// Register the entry for 32-bit hosts too. Off by default, and not
	// possible without the x86 wrapper beside the 64-bit one.
	bool isHost32() const {return selected.host32;}
	void setHost32(bool host32) {selected.host32 = host32;}
	bool canHost32() const;

	// Where the engine host publishes what it saw for a target.
	static std::wstring factsKey(const std::wstring& targetClsid);

private:
	void loadState();
	// The product's install directory (the InstallPath value its install hook
	// writes). The required form throws DeviceException when the value is
	// missing, as the endpoint adapter does; the optional form answers empty.
	static std::wstring requiredInstallDirectory(const IRegistry& from);
	static std::wstring optionalInstallDirectory(const IRegistry& from);
	// The bodies of the three operations, each inside the transaction
	// ReportedOperation::run opens (audit #348 C2/TD-31).
	void beginReport(DeviceInstallReport::Operation operation);
	void installWithin(RegistryTransaction& plan);
	void uninstallWithin(RegistryTransaction& plan);

	eapo::asio::AsioTarget target;
	bool input;
	IRegistry& registry;
	bool installed = false;
	eapo::asio::EntryOptions current;
	eapo::asio::EntryOptions selected;
	unsigned channelCount = 0;
	unsigned sampleRate = 0;
};
