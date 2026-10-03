/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "devices/ReceiverEndpoints.h"

#include "devices/DeviceAPOInfoKeys.h"
#include "services/logging/Logging.h"
#include "services/registry/IRegistry.h"
#include "services/registry/RegistryError.h"

std::vector<std::wstring> receivingEndpoints(const IRegistry& registry)
{
	std::vector<std::wstring> result;
	std::vector<std::wstring> names;
	try
	{
		if (!registry.keyExists(childApoPath))
			return result;
		names = registry.enumSubKeys(childApoPath);
	}
	catch (const RegistryError& error)
	{
		LogFStatic(L"Send receivers: cannot enumerate Child APOs: %s", error.getMessage().c_str());
		return result;
	}

	for (const std::wstring& name : names)
	{
		try
		{
			const std::wstring key = std::wstring(childApoPath) + L"\\" + name;
			if (registry.valueExists(key, receiveFromEndpointsValueName)
				&& registry.readValue(key, receiveFromEndpointsValueName) != L"false"
				&& registry.keyExists(std::wstring(renderKeyPath) + L"\\" + name))
				result.push_back(name);
		}
		catch (const RegistryError& error)
		{
			LogFStatic(L"Send receivers: skipping %s: %s", name.c_str(), error.getMessage().c_str());
		}
	}
	return result;
}
