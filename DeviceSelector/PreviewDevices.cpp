/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "PreviewDevices.h"

namespace
{
class PreviewAPOInfo : public AbstractAPOInfo
{
public:
	PreviewAPOInfo(const std::wstring& connection, const std::wstring& device,
		bool input, bool installed, bool experimental, bool defaultDev, bool unplugged)
		: connection(connection), device(device), input(input), installed(installed),
		experimental(experimental), defaultDev(defaultDev), unplugged(unplugged)
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
	bool isExperimental() const override { return experimental; }
	bool isEnhancementsDisabled() const override { return false; }
	bool isDefaultDevice() const override { return defaultDev; }
	bool isDisabled() const override { return false; }
	bool isUnplugged() const override { return unplugged; }
	void install() override {}
	void uninstall() override {}
	void reinstall() override {}

private:
	std::wstring connection;
	std::wstring device;
	bool input;
	bool installed;
	bool experimental;
	bool defaultDev;
	bool unplugged;
};

std::shared_ptr<AbstractAPOInfo> make(const std::wstring& connection, const std::wstring& device,
	bool input, bool installed, bool experimental, bool defaultDev, bool unplugged)
{
	return std::make_shared<PreviewAPOInfo>(connection, device, input, installed, experimental, defaultDev, unplugged);
}
}

namespace PreviewDevices
{
std::vector<std::shared_ptr<AbstractAPOInfo>> playback()
{
	return {
		make(L"Speakers", L"TOPPING USB DAC", false, true, false, true, false),
		make(L"CABLE Input", L"VB-Audio Virtual Cable", false, false, false, false, false),
		make(L"Headphones", L"Realtek(R) Audio", false, false, false, false, false),
		make(L"Digital Output", L"NVIDIA High Definition Audio", false, false, false, false, true),
	};
}

std::vector<std::shared_ptr<AbstractAPOInfo>> capture()
{
	return {
		make(L"Microphone", L"USB Audio Device", true, false, true, true, false),
		make(L"CABLE Output", L"VB-Audio Virtual Cable", true, false, true, false, false),
	};
}
}
