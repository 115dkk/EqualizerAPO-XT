/*
	This file is part of EqualizerAPO-XT.

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
#include "devices/AbstractAPOInfo.h"
#include "services/registry/IRegistry.h"

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
	bool isExperimental() const override;
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

	// The synchronous mode (no extra buffer; a missed deadline passes the
	// buffer through) as the Device Selector's option for this target. Both
	// directions share the setting, since they share the wrapper record.
	bool isSynchronous() const {return selectedSynchronous;}
	void setSynchronous(bool synchronous) {selectedSynchronous = synchronous;}

	// Where the engine host publishes what it saw for a target.
	static std::wstring factsKey(const std::wstring& targetClsid);

private:
	void loadState();
	std::wstring installDirectory() const;

	eapo::asio::AsioTarget target;
	bool input;
	IRegistry& registry;
	bool installed = false;
	bool currentSynchronous = false;
	bool selectedSynchronous = false;
	unsigned channelCount = 0;
	unsigned sampleRate = 0;
};
