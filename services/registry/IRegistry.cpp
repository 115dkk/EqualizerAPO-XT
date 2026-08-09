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

#include "stdafx.h"

#include "services/registry/IRegistry.h"
#include "services/registry/WindowsRegistry.h"

namespace
{
// Every method here is a single forwarding call and must stay that way. The
// adapter exists to give WindowsRegistry's static functions an overridable shape,
// not to add behaviour: no argument massaging, no extra existence checks, no
// translating RegistryError into anything else. If a rule ever belongs on
// top of a registry operation, it belongs in the device layer above the port,
// where the fake can see it too. Anything added here would be invisible in tests
// and would be running only on real machines - the one place this refactoring
// cannot be observed.
class SystemRegistry : public IRegistry
{
public:
	std::wstring readValue(const std::wstring& key, const std::wstring& valuename) const override
	{
		return WindowsRegistry::readValue(key, valuename);
	}

	unsigned long readDWORDValue(const std::wstring& key, const std::wstring& valuename) const override
	{
		return WindowsRegistry::readDWORDValue(key, valuename);
	}

	std::vector<std::wstring> readMultiValue(const std::wstring& key, const std::wstring& valuename) const override
	{
		return WindowsRegistry::readMultiValue(key, valuename);
	}

	std::vector<unsigned char> readBinaryValue(const std::wstring& key, const std::wstring& valuename) const override
	{
		return WindowsRegistry::readBinaryValue(key, valuename);
	}

	std::vector<std::wstring> enumSubKeys(const std::wstring& key) const override
	{
		return WindowsRegistry::enumSubKeys(key);
	}

	std::vector<std::wstring> enumValues(const std::wstring& key) const override
	{
		return WindowsRegistry::enumValues(key);
	}

	bool keyExists(const std::wstring& key) const override
	{
		return WindowsRegistry::keyExists(key);
	}

	bool valueExists(const std::wstring& key, const std::wstring& valuename) const override
	{
		return WindowsRegistry::valueExists(key, valuename);
	}

	bool keyEmpty(const std::wstring& key) const override
	{
		return WindowsRegistry::keyEmpty(key);
	}

	void writeValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) override
	{
		WindowsRegistry::writeValue(key, valuename, value);
	}

	void writeDWORDValue(const std::wstring& key, const std::wstring& valuename, unsigned long value) override
	{
		WindowsRegistry::writeDWORDValue(key, valuename, value);
	}

	void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) override
	{
		WindowsRegistry::writeMultiValue(key, valuename, value);
	}

	void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::vector<std::wstring>& values) override
	{
		WindowsRegistry::writeMultiValue(key, valuename, values);
	}

	void deleteValue(const std::wstring& key, const std::wstring& valuename) override
	{
		WindowsRegistry::deleteValue(key, valuename);
	}

	void createKey(const std::wstring& key) override
	{
		WindowsRegistry::createKey(key);
	}

	void deleteKey(const std::wstring& key) override
	{
		WindowsRegistry::deleteKey(key);
	}

	void takeOwnership(const std::wstring& key) override
	{
		WindowsRegistry::takeOwnership(key);
	}

	void makeWritable(const std::wstring& key) override
	{
		WindowsRegistry::makeWritable(key);
	}

	void saveToFile(const std::wstring& key, const std::vector<std::wstring>& valuenames, const std::wstring& filepath) override
	{
		WindowsRegistry::saveToFile(key, valuenames, filepath);
	}
};
}

IRegistry& systemRegistry()
{
	// Stateless, so the concurrent first calls the APO DLL can produce are safe:
	// C++11 guarantees the initialization itself is thread-safe, and there is no
	// shared state afterwards for callers to race over.
	static SystemRegistry instance;
	return instance;
}
