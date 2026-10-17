/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "asio/AsioRegistration.h"
#include "asio/WrapperRecord.h"
#include "devices/DeviceException.h"
#include "services/registry/RegistryPaths.h"

namespace AsioEntryWriter
{
enum class DirectoryRequirement { Optional, Required };

inline std::wstring installDirectory(const IRegistry& registry, DirectoryRequirement requirement)
{
	const auto directory = registry.valueExists(APP_REGPATH, L"InstallPath")
		? registry.readValue(APP_REGPATH, L"InstallPath") : std::wstring();
	if (directory.empty() && requirement == DirectoryRequirement::Required)
		throw DeviceException(L"The ASIO entry needs the InstallPath value under HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO");
	return directory;
}

inline void writeEntry(IRegistry& registry, const eapo::asio::AsioTarget& target,
	eapo::asio::WrapperRecord& record, const eapo::asio::EntryOptions& options, const std::wstring& directory)
{
	namespace registration = eapo::asio::AsioRegistration;
	eapo::asio::WrapperRecords::setEntryOptions(record, options);
	eapo::asio::WrapperRecords::write(registry, record);
	// Rebuild the registration views without discarding the merged record.
	// registerWrapper alone does not remove an earlier 32-bit registration.
	registration::unregisterWrapper(registry, target);
	registration::registerWrapper(registry, target, registration::wrapperDllPath(directory),
		record.register32 && registration::wrapper32Shipped(directory)
			? registration::wrapper32DllPath(directory) : std::wstring());
	registration::refreshAutoStart(registry, directory);
}

// The caller clears only the direction it owns. Preserve any remaining one.
inline void removeEntry(IRegistry& registry, const eapo::asio::AsioTarget& target,
	const eapo::asio::WrapperRecord& record, const std::wstring& directory)
{
	if (record.options.processInput || record.options.processOutput)
		eapo::asio::WrapperRecords::write(registry, record);
	else
	{
		eapo::asio::WrapperRecords::remove(registry, record.wrapperClsid);
		eapo::asio::AsioRegistration::unregisterWrapper(registry, target);
	}
	eapo::asio::AsioRegistration::refreshAutoStart(registry, directory);
}
}
