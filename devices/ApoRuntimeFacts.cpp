/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "devices/ApoRuntimeFacts.h"

#include "devices/DeviceAPOInfo.h"
#include "devices/DeviceAPOInfoKeys.h"

std::optional<ApoRuntimeFacts> readApoRuntimeFacts(IRegistry& registry, const std::wstring& deviceGuid, bool preMix)
{
	DeviceAPOInfo info(registry);
	if (!info.loadFromRegistry(deviceGuid))
		return std::nullopt;

	ApoRuntimeFacts facts;
	facts.capture = info.isInput();
	facts.postMixInstalled = info.getCurrentInstallState().installPostMix;
	facts.deviceName = info.getDeviceName();
	facts.connectionName = info.getConnectionName();
	facts.deviceGuid = info.getDeviceGuid();
	const std::wstring& child = preMix ? info.getPreMixChildGuid() : info.getPostMixChildGuid();
	if (isChildApoGuid(child))
		facts.childApoGuid = child;
	facts.allowSilentBufferModification = info.getCurrentInstallState().allowSilentBufferModification;
	return facts;
}

bool isChildApoGuid(const std::wstring& guid)
{
	return !guid.empty() && guid != APOGUID_NULL && guid != APOGUID_NOKEY && guid != APOGUID_NOVALUE;
}
