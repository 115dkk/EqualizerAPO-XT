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
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/Win32Resource.h"

// {EACD2258-FCAC-4FF4-B36D-419E924A6D79}
const GUID EQUALIZERAPO_PRE_MIX_GUID = {0xeacd2258, 0xfcac, 0x4ff4, {0xb3, 0x6d, 0x41, 0x9e, 0x92, 0x4a, 0x6d, 0x79}};
// {EC1CC9CE-FAED-4822-828A-82A81A6F018F}
const GUID EQUALIZERAPO_POST_MIX_GUID = {0xec1cc9ce, 0xfaed, 0x4822, {0x82, 0x8a, 0x82, 0xa8, 0x1a, 0x6f, 0x01, 0x8f}};

#define APP_REGPATH L"HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO"
#define USER_REGPATH L"HKEY_CURRENT_USER\\SOFTWARE\\EqualizerAPO"
// The Editor's preference tree (language, skin, window state). Defined here
// beside its parent path because the shared Qt app bootstrap reads the
// language preference for all three Qt apps.
#define EDITOR_REGPATH USER_REGPATH L"\\Configuration Editor"

class RegistryHelper
{
public:
	static std::wstring readValue(const std::wstring& key, const std::wstring& valuename);
	static unsigned long readDWORDValue(const std::wstring& key, const std::wstring& valuename);
	static std::vector<std::wstring> readMultiValue(const std::wstring& key, const std::wstring& valuename);
	static std::vector<unsigned char> readBinaryValue(const std::wstring& key, const std::wstring& valuename);
	static void writeValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value);
	static void writeDWORDValue(const std::wstring& key, const std::wstring& valuename, unsigned long value);
	static void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value);
	static void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::vector<std::wstring>& values);
	static void deleteValue(const std::wstring& key, const std::wstring& valuename);
	static void createKey(const std::wstring& key);
	static void deleteKey(const std::wstring& key);
	static void makeWritable(const std::wstring& key);
	static void takeOwnership(const std::wstring& key);
	static std::vector<std::wstring> enumSubKeys(const std::wstring& key);
	static std::vector<std::wstring> enumValues(const std::wstring& key);
	static bool keyExists(const std::wstring& key);
	static bool valueExists(const std::wstring& key, const std::wstring& valuename);
	static bool keyEmpty(const std::wstring& key);
	static std::wstring formatExportHeader(const std::wstring& key);
	static void saveToFile(const std::wstring& key, const std::vector<std::wstring>& valuenames, const std::wstring& filepath);
	static std::wstring getGuidString(GUID guid);
	static winutil::UniqueRegistryKey openKey(const std::wstring& key, REGSAM samDesired);

private:
	static std::wstring splitKey(const std::wstring& key, HKEY* rootKey);
};

class RegistryException
{
public:
	RegistryException(const std::wstring& message)
		: message(message) {}

	const std::wstring& getMessage() const
	{
		return message;
	}

private:
	std::wstring message;
};
