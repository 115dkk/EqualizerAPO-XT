/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	A device error that is not a registry failure (a missing InstallPath, an
	endpoint that is gone). One of the two types the device adapters'
	install, uninstall and reinstall throw (AbstractAPOInfo.h). Its own
	header so the ASIO adapter can throw it without the endpoint adapter's.
*/

#pragma once

#include <string>

#include "runtime/errors/WideError.h"

class DeviceException : public WideError
{
public:
	DeviceException(const std::wstring& message)
		: WideError(message)
	{
	}
};
