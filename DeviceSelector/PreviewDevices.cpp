/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "PreviewDevices.h"

namespace
{
class PreviewAPOInfo : public AbstractAPOInfo
{
public:
	PreviewAPOInfo(const std::wstring& connection, const std::wstring& device,
		bool input, bool installed, bool defaultDev, bool unplugged,
		AbstractAPOInfo::Kind kind = AbstractAPOInfo::Kind::Preview)
		: connection(connection), device(device), input(input), installed(installed),
		defaultDev(defaultDev), unplugged(unplugged), deviceKind(kind)
	{
	}

	std::wstring getConnectionName() const override { return connection; }
	std::wstring getDeviceName() const override { return device; }
	std::wstring getDeviceGuid() const override { return L"{preview}"; }
	std::wstring getDeviceString() const override { return connection + L" " + device; }
	unsigned getChannelCount() const override { return 2; }
	unsigned getSampleRate() const override { return 48000; }
	unsigned long getChannelMask() const override { return 3; }
	bool isInput() const override { return input; }
	bool isInstalled() const override { return installed; }
	bool canBeUpgraded() const override { return false; }
	bool hasChanges() const override { return false; }
	bool isEnhancementsDisabled() const override { return false; }
	bool isDefaultDevice() const override { return defaultDev; }
	bool isDisabled() const override { return false; }
	bool isUnplugged() const override { return unplugged; }
	Kind kind() const override { return deviceKind; }
	std::wstring getTransportLabel() const override { return deviceKind == Kind::Asio ? L"ASIO" : L""; }
	void install() override {}
	void uninstall() override {}
	void reinstall() override {}

private:
	std::wstring connection;
	std::wstring device;
	bool input;
	bool installed;
	bool defaultDev;
	bool unplugged;
	Kind deviceKind;
};

std::shared_ptr<AbstractAPOInfo> make(const std::wstring& connection, const std::wstring& device,
	bool input, bool installed, bool defaultDev, bool unplugged,
	AbstractAPOInfo::Kind kind = AbstractAPOInfo::Kind::Preview)
{
	return std::make_shared<PreviewAPOInfo>(connection, device, input, installed, defaultDev, unplugged, kind);
}
}

namespace PreviewDevices
{
std::vector<std::shared_ptr<AbstractAPOInfo>> playback()
{
	return {
		make(L"Speakers", L"TOPPING USB DAC", false, true, true, false),
		make(L"CABLE Input", L"VB-Audio Virtual Cable", false, false, false, false),
		make(L"Headphones", L"Realtek(R) Audio", false, false, false, false),
		make(L"Digital Output", L"NVIDIA High Definition Audio", false, false, false, true),
		// ASIO targets sit in the same group as the endpoints; the leading
		// word is the one thing that tells them apart
		// (docs/architecture/asio-host-study.md, 9).
		make(L"ASIO", L"Topping USB Audio Device", false, true, false, false, AbstractAPOInfo::Kind::Asio),
		make(L"ASIO", L"miniDSP ASIO Driver", false, false, false, false, AbstractAPOInfo::Kind::Asio),
	};
}

std::vector<std::shared_ptr<AbstractAPOInfo>> capture()
{
	return {
		make(L"Microphone", L"USB Audio Device", true, false, true, false),
		make(L"CABLE Output", L"VB-Audio Virtual Cable", true, false, false, false),
		make(L"ASIO", L"miniDSP ASIO Driver", true, false, false, false, AbstractAPOInfo::Kind::Asio),
	};
}
}
