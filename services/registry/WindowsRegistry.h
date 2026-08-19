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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/Win32Resource.h"
#include "services/registry/IRegistry.h"
#include "services/registry/RegistryError.h"

// The live adapter IS the port implementation (audit #275 C1): the former
// 142-line SystemRegistry forwarder in IRegistry.cpp existed only to give
// these operations an overridable shape, and every one of its methods was a
// single forwarding call by decree. With the port methods implemented here
// directly, the static facade spelling (WindowsRegistry::readValue) and the
// port spelling (systemRegistry().readValue) can no longer drift - there is
// one spelling, and the callers that used to bypass the port now share the
// seam FakeRegistry plugs into.
//
// Only the operations that expose Win32 types (openKey's REGSAM/handle) or
// registry export syntax (formatExportHeader) stay static: they are
// live-adapter-only by nature and deliberately outside the port.
class WindowsRegistry final : public IRegistry
{
public:
	std::wstring readValue(const std::wstring& key, const std::wstring& valuename) const override;
	unsigned long readDWORDValue(const std::wstring& key, const std::wstring& valuename) const override;
	std::vector<std::wstring> readMultiValue(const std::wstring& key, const std::wstring& valuename) const override;
	std::vector<unsigned char> readBinaryValue(const std::wstring& key, const std::wstring& valuename) const override;
	void writeValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) override;
	void writeDWORDValue(const std::wstring& key, const std::wstring& valuename, unsigned long value) override;
	void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) override;
	void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::vector<std::wstring>& values) override;
	void deleteValue(const std::wstring& key, const std::wstring& valuename) override;
	void createKey(const std::wstring& key) override;
	void deleteKey(const std::wstring& key) override;
	void makeWritable(const std::wstring& key) override;
	void takeOwnership(const std::wstring& key) override;
	std::vector<std::wstring> enumSubKeys(const std::wstring& key) const override;
	std::vector<std::wstring> enumValues(const std::wstring& key) const override;
	bool keyExists(const std::wstring& key) const override;
	bool valueExists(const std::wstring& key, const std::wstring& valuename) const override;
	bool keyEmpty(const std::wstring& key) const override;
	void saveToFile(const std::wstring& key, const std::vector<std::wstring>& valuenames, const std::wstring& filepath) override;

	static std::wstring formatExportHeader(const std::wstring& key);
	static winutil::UniqueRegistryKey openKey(const std::wstring& key, REGSAM samDesired);

private:
	static std::wstring splitKey(const std::wstring& key, HKEY* rootKey);
};
