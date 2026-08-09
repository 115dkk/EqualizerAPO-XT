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
#include "text/WideString.h"
#include "platform/windows/Win32Error.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <ObjBase.h>
#include <aclapi.h>
#include <authz.h>

#include "services/registry/WindowsRegistry.h"
#include "platform/windows/Win32Resource.h"

using std::endl;
using std::find;
using std::string;
using std::vector;
using std::wofstream;
using std::wstring;


wstring WindowsRegistry::readValue(const wstring& key, const wstring& valuename)
{
	wstring result;

	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_QUERY_VALUE | KEY_WOW64_64KEY));

	LSTATUS status;
	DWORD type;
	DWORD bufSize;
	status = RegQueryValueExW(keyHandle.get(), valuename.c_str(), nullptr, &type, nullptr, &bufSize);
	if (status != ERROR_SUCCESS)
	{
		throw RegistryError(L"Error while reading registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
	}

	if (type != REG_SZ)
	{
		throw RegistryError(L"Registry value " + key + L"\\" + valuename + L" has wrong type");
	}

	vector<wchar_t> buf(bufSize / sizeof(wchar_t) + 1);
	status = RegQueryValueExW(keyHandle.get(), valuename.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buf.data()), &bufSize);

	if (status != ERROR_SUCCESS)
	{
		throw RegistryError(L"Error while reading registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
	}

	// Audit #250 F038: a zero-length REG_SZ used to underflow the
	// termination check's index (bufSize / sizeof(wchar_t) - 1) into an
	// out-of-bounds read. An empty value is simply the empty string.
	if (bufSize < sizeof(wchar_t))
		return wstring();

	// Remove zero-termination
	if (buf[bufSize / sizeof(wchar_t) - 1] == L'\0')
		bufSize -= sizeof(wchar_t);
	result = wstring(buf.data(), (wstring::size_type)bufSize / sizeof(wchar_t));

	return result;
}

unsigned long WindowsRegistry::readDWORDValue(const wstring& key, const wstring& valuename)
{
	unsigned long result;

	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_QUERY_VALUE | KEY_WOW64_64KEY));

	LSTATUS status;
	DWORD type;
	DWORD bufSize;
	status = RegQueryValueExW(keyHandle.get(), valuename.c_str(), nullptr, &type, nullptr, &bufSize);
	if (status != ERROR_SUCCESS)
	{
		throw RegistryError(L"Error while reading registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
	}

	if (type != REG_DWORD)
	{
		throw RegistryError(L"Registry value " + key + L"\\" + valuename + L" has wrong type");
	}

	status = RegQueryValueExW(keyHandle.get(), valuename.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(&result), &bufSize);

	if (status != ERROR_SUCCESS)
	{
		throw RegistryError(L"Error while reading registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
	}

	return result;
}

vector<wstring> WindowsRegistry::readMultiValue(const wstring& key, const wstring& valuename)
{
	vector<wstring> result;

	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_QUERY_VALUE | KEY_WOW64_64KEY));

	LSTATUS status;
	DWORD type;
	DWORD bufSize;
	status = RegQueryValueExW(keyHandle.get(), valuename.c_str(), nullptr, &type, nullptr, &bufSize);
	if (status != ERROR_SUCCESS)
	{
		throw RegistryError(L"Error while reading registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
	}

	if (type != REG_MULTI_SZ)
	{
		throw RegistryError(L"Registry value " + key + L"\\" + valuename + L" has wrong type");
	}

	vector<wchar_t> buf(bufSize / sizeof(wchar_t) + 1);
	status = RegQueryValueExW(keyHandle.get(), valuename.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buf.data()), &bufSize);

	if (status != ERROR_SUCCESS)
	{
		throw RegistryError(L"Error while reading registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
	}

	size_t length = bufSize / sizeof(wchar_t);
	// Remove zero-termination
	while (length > 0 && buf[length - 1] == L'\0')
		length--;

	size_t start = 0;
	for (size_t i = 0; i < length; i++)
	{
		if (buf[i] == L'\0')
		{
			result.push_back(wstring(buf.data() + start, i - start));
			start = i + 1;
		}
	}

	if (length > start)
		result.push_back(wstring(buf.data() + start, length - start));

	return result;
}

vector<unsigned char> WindowsRegistry::readBinaryValue(const wstring& key, const wstring& valuename)
{
	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_QUERY_VALUE | KEY_WOW64_64KEY));

	LSTATUS status;
	DWORD type;
	DWORD bufSize;
	status = RegQueryValueExW(keyHandle.get(), valuename.c_str(), nullptr, &type, nullptr, &bufSize);
	if (status != ERROR_SUCCESS)
	{
		throw RegistryError(L"Error while reading registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
	}

	if (type != REG_BINARY)
	{
		throw RegistryError(L"Registry value " + key + L"\\" + valuename + L" has wrong type");
	}

	vector<unsigned char> result(bufSize, 0);
	status = RegQueryValueExW(keyHandle.get(), valuename.c_str(), nullptr, nullptr, result.data(), &bufSize);

	if (status != ERROR_SUCCESS)
	{
		throw RegistryError(L"Error while reading registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
	}

	return result;
}

void WindowsRegistry::writeValue(const wstring& key, const wstring& valuename, const wstring& value)
{
	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_SET_VALUE | KEY_WOW64_64KEY));

	LSTATUS status = RegSetValueExW(keyHandle.get(), valuename.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));

	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while writing to registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
}

void WindowsRegistry::writeDWORDValue(const wstring& key, const wstring& valuename, unsigned long value)
{
	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_SET_VALUE | KEY_WOW64_64KEY));

	LSTATUS status = RegSetValueExW(keyHandle.get(), valuename.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(unsigned long));

	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while writing to registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
}

void WindowsRegistry::writeMultiValue(const wstring& key, const wstring& valuename, const wstring& value)
{
	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_SET_VALUE | KEY_WOW64_64KEY));

	wstring data = value;
	data.push_back(L'\0');
	data.push_back(L'\0');

	LSTATUS status = RegSetValueExW(keyHandle.get(), valuename.c_str(), 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(data.data()), static_cast<DWORD>(data.size() * sizeof(wchar_t)));

	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while writing to registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
}

void WindowsRegistry::writeMultiValue(const wstring& key, const wstring& valuename, const vector<wstring>& values)
{
	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_SET_VALUE | KEY_WOW64_64KEY));

	size_t size = 1;
	for (const wstring& value : values)
		size += value.size() + 1;

	wstring data;
	data.reserve(size);
	for (const wstring& value : values)
	{
		data.append(value);
		data.push_back(L'\0');
	}
	data.push_back(L'\0');

	LSTATUS status = RegSetValueExW(keyHandle.get(), valuename.c_str(), 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(data.data()), static_cast<DWORD>(data.size() * sizeof(wchar_t)));

	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while writing to registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
}

void WindowsRegistry::deleteValue(const wstring& key, const wstring& valuename)
{
	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_SET_VALUE | KEY_WOW64_64KEY));

	LSTATUS status = RegDeleteValueW(keyHandle.get(), valuename.c_str());

	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while deleting registry value " + key + L"\\" + valuename + L": " + win32::errorMessage(status));
}

void WindowsRegistry::createKey(const wstring& key)
{
	HKEY rootKey;
	wstring subKey = splitKey(key, &rootKey);

	winutil::UniqueRegistryKey keyHandle;
	LSTATUS status = RegCreateKeyExW(rootKey, subKey.c_str(), 0, nullptr, 0,
		KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, keyHandle.put(), nullptr);
	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while creating registry key " + key + L": " + win32::errorMessage(status));

}

void WindowsRegistry::deleteKey(const wstring& key)
{
	HKEY rootKey;
	wstring subKey = splitKey(key, &rootKey);

	LSTATUS status = RegDeleteKeyExW(rootKey, subKey.c_str(), KEY_WOW64_64KEY, 0);
	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while deleting registry key " + key + L": " + win32::errorMessage(status));
}

void WindowsRegistry::makeWritable(const wstring& key)
{
	winutil::UniqueRegistryKey keyHandle(openKey(key, READ_CONTROL | WRITE_DAC | KEY_WOW64_64KEY));

	DWORD descriptorSize = 0;
	RegGetKeySecurity(keyHandle.get(), DACL_SECURITY_INFORMATION, nullptr, &descriptorSize);

	winutil::UniqueProcessHeapPtr<void> oldSd(
		HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, descriptorSize));
	if (!oldSd)
		throw RegistryError(L"HeapAlloc failed while ensuring registry key writability");
	LSTATUS status = RegGetKeySecurity(keyHandle.get(), DACL_SECURITY_INFORMATION, oldSd.get(), &descriptorSize);
	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while getting security information for registry key " + key + L": " + win32::errorMessage(status));

	BOOL aclPresent, aclDefaulted;
	PACL oldAcl = nullptr;
	if (!GetSecurityDescriptorDacl(oldSd.get(), &aclPresent, &oldAcl, &aclDefaulted))
		throw RegistryError(L"Error in GetSecurityDescriptorDacl while ensuring writability");

	winutil::UniqueSid sid;
	SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0, sid.put()))
		throw RegistryError(L"Error in AllocateAndInitializeSid while ensuring writability");

	EXPLICIT_ACCESS ea;
	ea.grfAccessPermissions = KEY_ALL_ACCESS;
	ea.grfAccessMode = SET_ACCESS;
	ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
	ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
	ea.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
	ea.Trustee.ptstrName = static_cast<LPWSTR>(sid.get());

	winutil::UniqueLocalPtr<ACL> acl;
	if (ERROR_SUCCESS != SetEntriesInAcl(1, &ea, oldAcl, acl.put()))
		throw RegistryError(L"Error in SetEntriesInAcl while ensuring writability");

	winutil::UniqueLocalPtr<void> sd(LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH));
	if (!sd)
		throw RegistryError(L"Error in LocalAlloc while ensuring writability");

	if (!InitializeSecurityDescriptor(sd.get(), SECURITY_DESCRIPTOR_REVISION))
		throw RegistryError(L"Error in InitializeSecurityDescriptor while ensuring writability");

	if (!SetSecurityDescriptorDacl(sd.get(), TRUE, acl.get(), FALSE))
		throw RegistryError(L"Error in SetSecurityDescriptorDacl while ensuring writability");

	status = RegSetKeySecurity(keyHandle.get(), DACL_SECURITY_INFORMATION, sd.get());
	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while setting security information for registry key " + key + L": " + win32::errorMessage(status));
}

void WindowsRegistry::takeOwnership(const wstring& key)
{
	winutil::UniqueHandle tokenHandle;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, tokenHandle.put()))
		throw RegistryError(L"Error in OpenProcessToken while taking ownership");

	LUID luid;
	if (!LookupPrivilegeValue(nullptr, SE_TAKE_OWNERSHIP_NAME, &luid))
		throw RegistryError(L"Error in LookupPrivilegeValue while taking ownership");

	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	if (!AdjustTokenPrivileges(tokenHandle.get(), FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr))
		throw RegistryError(L"Error in AdjustTokenPrivileges while taking ownership");

	winutil::UniqueRegistryKey keyHandle(openKey(key, WRITE_OWNER | KEY_WOW64_64KEY));

	winutil::UniqueLocalPtr<void> sd(LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH));
	if (!sd)
		throw RegistryError(L"Error in SetPrivilege while taking ownership");

	if (!InitializeSecurityDescriptor(sd.get(), SECURITY_DESCRIPTOR_REVISION))
		throw RegistryError(L"Error in InitializeSecurityDescriptor while taking ownership");

	winutil::UniqueSid sid;
	SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0, sid.put()))
		throw RegistryError(L"Error in AllocateAndInitializeSid while taking ownership");

	if (!SetSecurityDescriptorOwner(sd.get(), sid.get(), FALSE))
		throw RegistryError(L"Error in SetSecurityDescriptorOwner while taking ownership");

	LSTATUS status = RegSetKeySecurity(keyHandle.get(), OWNER_SECURITY_INFORMATION, sd.get());
	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while setting security information for registry key " + key + L": " + win32::errorMessage(status));

	tp.Privileges[0].Attributes = 0;

	if (!AdjustTokenPrivileges(tokenHandle.get(), FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr))
		throw RegistryError(L"Error in AdjustTokenPrivileges while taking ownership");
}

bool WindowsRegistry::keyExists(const wstring& key)
{
	bool result;

	HKEY rootKey;
	wstring subKey = splitKey(key, &rootKey);

	winutil::UniqueRegistryKey keyHandle;
	result = (RegOpenKeyExW(rootKey, subKey.c_str(), 0,
		KEY_QUERY_VALUE | KEY_WOW64_64KEY, keyHandle.put()) == ERROR_SUCCESS);

	return result;
}

bool WindowsRegistry::valueExists(const wstring& key, const wstring& valuename)
{
	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_QUERY_VALUE | KEY_WOW64_64KEY));

	DWORD type;
	DWORD bufSize;
	LSTATUS status = RegQueryValueExW(keyHandle.get(), valuename.c_str(), nullptr, &type, nullptr, &bufSize);
	return status == ERROR_SUCCESS;
}

vector<wstring> WindowsRegistry::enumSubKeys(const wstring& key)
{
	vector<wstring> result;

	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_ENUMERATE_SUB_KEYS | KEY_WOW64_64KEY));

	wchar_t keyName[256];
	DWORD keyLength = sizeof(keyName) / sizeof(wchar_t);
	int i = 0;

	LSTATUS status;
	while ((status = RegEnumKeyExW(keyHandle.get(), i++, keyName, &keyLength, nullptr, nullptr, nullptr, nullptr)) == ERROR_SUCCESS)
	{
		keyLength = sizeof(keyName) / sizeof(wchar_t);

		result.push_back(keyName);
	}

	if (status != ERROR_NO_MORE_ITEMS)
		throw RegistryError(L"Error while enumerating sub keys of registry key " + key + L": " + win32::errorMessage(status));

	return result;
}

vector<wstring> WindowsRegistry::enumValues(const wstring& key)
{
	vector<wstring> result;

	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_QUERY_VALUE | KEY_WOW64_64KEY));

	// 16383 is the documented maximum length of a registry value name, and the
	// names this codebase deals with are property-key strings well inside it, so
	// one buffer of that size means the loop never has to grow anything.
	vector<wchar_t> valueName(16384);
	DWORD nameLength = static_cast<DWORD>(valueName.size());
	DWORD index = 0;

	LSTATUS status;
	while ((status = RegEnumValueW(keyHandle.get(), index++, valueName.data(), &nameLength, nullptr, nullptr, nullptr, nullptr)) == ERROR_SUCCESS)
	{
		result.push_back(wstring(valueName.data(), nameLength));
		nameLength = static_cast<DWORD>(valueName.size());
	}

	if (status != ERROR_NO_MORE_ITEMS)
		throw RegistryError(L"Error while enumerating values of registry key " + key + L": " + win32::errorMessage(status));

	return result;
}

bool WindowsRegistry::keyEmpty(const wstring& key)
{
	winutil::UniqueRegistryKey keyHandle(openKey(key, KEY_QUERY_VALUE | KEY_WOW64_64KEY));

	DWORD keyCount;
	DWORD valueCount;
	LSTATUS status = RegQueryInfoKeyW(keyHandle.get(), nullptr, nullptr, nullptr, &keyCount, nullptr, nullptr, &valueCount, nullptr, nullptr, nullptr, nullptr);

	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while reading info for registry key " + key + L": " + win32::errorMessage(status));

	return keyCount == 0 && valueCount == 0;
}

void WindowsRegistry::saveToFile(const wstring& key, const vector<wstring>& valuenames, const wstring& filepath)
{
	wofstream stream(filepath);
	if (!stream.good())
		throw RegistryError(L"Error while opening file " + filepath + L" for writing");

	stream << L"Windows Registry Editor Version 5.00\n" << endl;
	stream << formatExportHeader(key) << endl;
	for (vector<wstring>::const_iterator it = valuenames.cbegin(); it != valuenames.cend(); it++)
	{
		const wstring& valuename = *it;
		wstring value = readValue(key, valuename);

		stream << L"\"" << valuename << L"\"=\"" << value << L"\"" << endl;
	}
	stream << endl;

	stream.close();
}

wstring WindowsRegistry::formatExportHeader(const wstring& key)
{
	return L"[" + key + L"]";
}

winutil::UniqueRegistryKey WindowsRegistry::openKey(const wstring& key, REGSAM samDesired)
{
	HKEY rootKey;
	wstring subKey = splitKey(key, &rootKey);

	winutil::UniqueRegistryKey keyHandle;
	LSTATUS status = RegOpenKeyExW(rootKey, subKey.c_str(), 0, samDesired, keyHandle.put());
	if (status != ERROR_SUCCESS)
		throw RegistryError(L"Error while opening registry key " + key + L": " + win32::errorMessage(status));

	return keyHandle;
}

wstring WindowsRegistry::splitKey(const wstring& key, HKEY* rootKey)
{
	size_t pos = key.find(L'\\');
	if (pos == wstring::npos)
		throw RegistryError(L"Key " + key + L" has invalid format");

	wstring rootPart = key.substr(0, pos);
	wstring pathPart = key.substr(pos + 1);

	wstring p = text::toUpper(rootPart);
	if (p == L"HKEY_CLASSES_ROOT")
		*rootKey = HKEY_CLASSES_ROOT;
	else if (p == L"HKEY_CURRENT_CONFIG")
		*rootKey = HKEY_CURRENT_CONFIG;
	else if (p == L"HKEY_CURRENT_USER")
		*rootKey = HKEY_CURRENT_USER;
	else if (p == L"HKEY_LOCAL_MACHINE")
		*rootKey = HKEY_LOCAL_MACHINE;
	else if (p == L"HKEY_USERS")
		*rootKey = HKEY_USERS;
	else
		throw RegistryError(L"Unknown root key " + rootPart);

	return pathPart;
}
