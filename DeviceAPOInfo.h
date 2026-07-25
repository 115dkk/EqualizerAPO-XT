/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2012  Jonas Thedering

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

#define APOGUID_NULL L"{00000000-0000-0000-0000-000000000000}"
#define APOGUID_NOKEY L"!KEY"
#define APOGUID_NOVALUE L"!VALUE"

class DeviceAPOInfo : public AbstractAPOInfo
{
public:
	enum InstallMode
	{
		INSTALL_LFX_GFX = 0,
		INSTALL_SFX_MFX = 1,
		INSTALL_SFX_EFX = 2
	};

	struct InstallState
	{
		bool installPreMix;
		bool installPostMix;
		bool useOriginalAPOPreMix;
		bool useOriginalAPOPostMix;
		bool autoAdjust;
		InstallMode installMode;
		bool allowSilentBufferModification;

		InstallState()
		{
			installPreMix = false;
			installPostMix = false;
			useOriginalAPOPreMix = false;
			useOriginalAPOPostMix = false;
			autoAdjust = true;
			installMode = INSTALL_LFX_GFX;
			allowSilentBufferModification = false;
		}

		bool operator!=(const InstallState& other) const
		{
			return installPreMix != other.installPreMix
				|| installPostMix != other.installPostMix
				|| useOriginalAPOPreMix != other.useOriginalAPOPreMix
				|| useOriginalAPOPostMix != other.useOriginalAPOPostMix
				|| autoAdjust != other.autoAdjust
				|| installMode != other.installMode
				|| allowSilentBufferModification != other.allowSilentBufferModification;
		}
	};

	// Every registry access this class makes goes through the injected port. The
	// default is the live adapter, so the existing callers (DeviceSelector,
	// Editor, ApoRegistration, the APO DLL) keep compiling unchanged and keep
	// talking to the real HKLM; a test hands in an in-memory fake instead.
	// getDefaultDevice takes none because it reads the endpoint through COM.
	explicit DeviceAPOInfo(IRegistry& registry = systemRegistry());

	static std::vector<std::shared_ptr<AbstractAPOInfo>> loadAllInfos(bool input, IRegistry& registry = systemRegistry());
	static std::wstring getDefaultDevice(bool input, int role = 1);
	static bool checkProtectedAudioDG(bool fix, IRegistry& registry = systemRegistry());
	// const because the fix branch registers the COM server by calling the DLL's
	// own DllRegisterServer, and never writes through the port.
	static bool checkAPORegistration(bool fix, const IRegistry& registry = systemRegistry());
	bool load(const std::wstring& deviceGuid, std::wstring defaultDeviceGuid = L"");
	bool canBeUpgraded() const override;
	bool hasChanges() const override;
	bool isExperimental() const override;
	std::wstring getOriginalAPOPreMix();
	std::wstring getOriginalAPOPostMix();
	void install() override;
	void uninstall() override;
	void reinstall() override;
	std::wstring getConnectionName() const override;
	std::wstring getDeviceName() const override;
	std::wstring getDeviceGuid() const override;
	std::wstring getDeviceString() const override;
	unsigned getChannelCount() const override;
	unsigned getSampleRate() const override;
	unsigned long getChannelMask() const override;
	bool isInput() const override;
	bool isInstalled() const override;
	bool isEnhancementsDisabled() const override;
	bool isDefaultDevice() const override;
	bool isDisabled() const override;
	bool isUnplugged() const override;
	const InstallState& getCurrentInstallState();
	InstallState& getSelectedInstallState();
	std::wstring getPreMixChildGuid();
	std::wstring getPostMixChildGuid();
	void testAPOInstallation();

private:
	void fail(const std::wstring& functionName, HRESULT hr);

	std::wstring deviceName;
	std::wstring connectionName;
	std::wstring deviceGuid;
	unsigned channelCount = 0;
	unsigned sampleRate = 0;
	unsigned long channelMask = 0;
	bool defaultDevice = false;
	bool enhancementsDisabled = false;
	bool disabled = false;
	bool unplugged = false;

	// used for creating child APO
	std::wstring preMixChildGuid;
	std::wstring postMixChildGuid;

	// used for uninstallation
	std::wstring originalApoGuids[5];

	bool input = false;
	bool installed = false;
	std::wstring version;
	InstallState currentInstallState;
	// selection in GUI
	InstallState selectedInstallState;

	// A reference, not a value: the adapter is a process-wide singleton and the
	// fake outlives the info object in a test, so there is nothing to own here.
	// It also makes the class non-assignable, which is harmless - every
	// DeviceAPOInfo in the tree is constructed in place and used through a
	// shared_ptr or as a local.
	IRegistry& registry;
};

class DeviceException
{
public:
	DeviceException(const std::wstring& message)
		: message(message)
	{
	}

	const std::wstring& getMessage() const
	{
		return message;
	}

private:
	std::wstring message;
};
