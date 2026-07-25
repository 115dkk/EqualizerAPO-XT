/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2017  Jonas Thedering

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

#pragma once

#include <string>
#include <vector>
#include <memory>
#include "AbstractAPOInfo.h"
#include "helpers/IRegistry.h"

class VoicemeeterAPOInfo : public AbstractAPOInfo
{
public:
	// Same injection as DeviceAPOInfo, for the same reason: prependInfos is
	// called from DeviceAPOInfo::loadAllInfos, so it has to be able to pass the
	// registry it was given down. Only the registry reads are routed through the
	// port; the startup-shortcut and process work stays on the Win32 calls.
	static void prependInfos(std::vector<std::shared_ptr<AbstractAPOInfo>>& list, IRegistry& registry = systemRegistry());

	VoicemeeterAPOInfo(const std::wstring& connectionName, bool voicemeeterInstalled, IRegistry& registry = systemRegistry());

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
	bool isVoicemeeterInstalled() const;
	void install() override;
	void uninstall() override;
	void reinstall() override;
	static void ensureVoicemeeterClientRunning();
	static void saveVoicemeeterSampleRate(unsigned sampleRate, IRegistry& registry = systemRegistry());

private:
	static std::wstring getStartupPath();
	static std::wstring getClientPath();
	static void createLink(const std::wstring& lnkPath, const std::wstring& path, const std::wstring& args);
	static std::wstring getLinkArgs(const std::wstring& lnkPath, std::wstring* path = nullptr);
	static std::vector<std::wstring> splitArgs(const std::wstring& argString);
	static std::wstring joinArgs(const std::vector<std::wstring>& args);
	static void closeProcess(unsigned long processId);

	std::wstring connectionName;
	unsigned sampleRate;
	bool installed;
	bool defaultDevice = false;
	bool voicemeeterInstalled;
	bool changes = false;
	IRegistry& registry;
};
