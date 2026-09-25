/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "asio/StreamFacts.h"

#include "services/registry/RegistryPaths.h"

namespace eapo::asio
{
	namespace
	{
		const wchar_t* const sampleRateValue = L"SampleRate";
		const wchar_t* const outputChannelsValue = L"OutputChannels";
		const wchar_t* const inputChannelsValue = L"InputChannels";
		// Written for diagnostics and read by nothing in the product: the
		// buffer size and the target's own name as the last stream saw them,
		// which is what a support request asks for first. The maintainer
		// decided to keep them (audit #348 C7).
		const wchar_t* const framesValue = L"Frames";
		const wchar_t* const deviceNameValue = L"DeviceName";

		unsigned long readDwordOr(const IRegistry& registry, const std::wstring& key, const wchar_t* name)
		{
			return registry.valueExists(key, name) ? registry.readDWORDValue(key, name) : 0;
		}
	}

	namespace StreamFacts
	{
		std::wstring key(const std::wstring& targetClsid)
		{
			return std::wstring(USER_REGPATH) + L"\\ASIO\\" + targetClsid;
		}

		void write(IRegistry& registry, const StreamFormat& format)
		{
			const std::wstring path = key(format.deviceGuid);
			registry.createKey(path);
			registry.writeDWORDValue(path, sampleRateValue, static_cast<unsigned long>(format.sampleRate));
			registry.writeDWORDValue(path, outputChannelsValue, format.channelCount(Direction::Output));
			registry.writeDWORDValue(path, inputChannelsValue, format.channelCount(Direction::Input));
			registry.writeDWORDValue(path, framesValue, format.frames);
			registry.writeValue(path, deviceNameValue, format.deviceName);
		}

		bool read(const IRegistry& registry, const std::wstring& targetClsid, StreamShape& shape)
		{
			shape = StreamShape();
			const std::wstring path = key(targetClsid);
			if (!registry.keyExists(path))
				return false;
			shape.sampleRate = readDwordOr(registry, path, sampleRateValue);
			shape.channels[static_cast<unsigned>(Direction::Output)] = readDwordOr(registry, path, outputChannelsValue);
			shape.channels[static_cast<unsigned>(Direction::Input)] = readDwordOr(registry, path, inputChannelsValue);
			return true;
		}
	}
}
