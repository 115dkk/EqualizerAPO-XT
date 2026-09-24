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

#include "DeviceInstallReport.h"

class AbstractAPOInfo
{
public:
	virtual ~AbstractAPOInfo();
	virtual std::wstring getConnectionName() const = 0;
	virtual std::wstring getDeviceName() const = 0;
	virtual std::wstring getDeviceGuid() const = 0;
	virtual std::wstring getDeviceString() const = 0;
	virtual unsigned getChannelCount() const = 0;
	virtual unsigned getSampleRate() const = 0;
	virtual unsigned long getChannelMask() const = 0;
	virtual bool isInput() const = 0;
	virtual bool isInstalled() const = 0;
	virtual bool canBeUpgraded() const = 0;
	virtual bool hasChanges() const = 0;
	virtual bool isEnhancementsDisabled() const = 0;
	virtual bool isDefaultDevice() const = 0;
	virtual bool isDisabled() const = 0;
	virtual bool isUnplugged() const = 0;
	// All three throw only exceptions derived from WideError
	// (runtime/errors/WideError.h): RegistryError when the registry refuses
	// a read or write, DeviceException for any other device error (a missing
	// InstallPath, an endpoint that is gone). A caller that does not care
	// which catches `const WideError&` (audit #348 C1/TD-32).
	virtual void install() = 0;
	virtual void uninstall() = 0;
	virtual void reinstall() = 0;

	// Whether an install, uninstall or reinstall of this record takes effect
	// only once the Windows audio service restarts, which Device Selector's
	// device test then checks. True for the endpoint APO chain; the ASIO
	// entry, the Voicemeeter shortcut and the gallery preview need no
	// restart. Device Selector used to answer this by casting to the
	// endpoint type (audit #348 C1).
	virtual bool changesNeedAudioRestart() const
	{
		return false;
	}

	// A short word the device lists append to the state text when the
	// device is reached through something other than a Windows endpoint
	// ("ASIO"). Empty for endpoints. The one marker such devices get: they
	// sit in the same playback/capture groups as everything else.
	virtual std::wstring getTransportLabel() const
	{
		return std::wstring();
	}

	// What the last install, uninstall or reinstall on this object did. The
	// caller needs this whether the operation succeeded or threw, so it is a
	// value on the object rather than a return type: the three functions are
	// declared void by an interface every device type implements, and the one
	// caller that matters - Device Selector - runs them inside a try block and
	// wants the report in both paths.
	//
	// Not pure virtual, and the storage is here rather than in each subclass,
	// because the device types whose change is not a registry transaction have
	// nothing to fill it with: the Voicemeeter type rewrites a startup shortcut
	// and the preview type used by the skin gallery does nothing at all. They
	// leave it at NotAttempted, which is the truth about them. The endpoint and
	// ASIO types fill it through ReportedOperation::run.
	const DeviceInstallReport& getLastOperationReport() const
	{
		return lastOperationReport;
	}

protected:
	DeviceInstallReport lastOperationReport;
};
