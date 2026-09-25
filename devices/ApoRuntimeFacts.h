/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What the APO needs to know about its endpoint when audiodg initializes it
	(audit #348 F11/TD-47): seven facts, read through the registry port.

	Initialize used to run the Device Selector's whole DeviceAPOInfo::load(),
	which also asks the MMDevice enumerator for the default endpoint over COM
	and reads the endpoint's ASIO entry, so a failing ASIO read threw away the
	child APO and the capture flag with it. This reads the same install record
	through DeviceAPOInfo::loadFromRegistry, so the rules for reading it stay in
	one place, and decides here which child APO GUID is a real one.
*/

#pragma once

#include <optional>
#include <string>

class IRegistry;

struct ApoRuntimeFacts
{
	bool capture = false;
	bool postMixInstalled = false;
	std::wstring deviceName;
	std::wstring connectionName;
	std::wstring deviceGuid;
	// The CLSID of the driver's own APO that this instance wraps, or empty
	// when the install recorded none.
	std::wstring childApoGuid;
	bool allowSilentBufferModification = false;
};

// Empty when the endpoint is not present. Throws RegistryError when its keys
// cannot be read, as DeviceAPOInfo::load does.
std::optional<ApoRuntimeFacts> readApoRuntimeFacts(IRegistry& registry, const std::wstring& deviceGuid, bool preMix);

// True for a child APO GUID the APO should create: not empty and none of the
// install record's sentinels (APOGUID_NULL, APOGUID_NOKEY, APOGUID_NOVALUE).
bool isChildApoGuid(const std::wstring& guid);
