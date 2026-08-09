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

// Deliberately no <windows.h> here, and that is the whole point of this file.
// WindowsRegistry.h has to pull the Win32 headers in because openKey takes a
// REGSAM and returns a handle wrapper; every translation unit that wants to talk
// to the registry therefore inherits the Win32 macro soup. This port names only
// the operations the device layer performs, all of which are expressible in
// standard types, so a test can include it and stand up a fake without a Windows
// toolchain in its include path.
//
// The method set is what devices/DeviceAPOInfo.{Install,Load,State,
// Uninstall}.cpp, DeviceAPOInfo.cpp and VoicemeeterAPOInfo.cpp call, plus the
// three RegistryTransaction needs to undo them, and it is meant to stay that
// way. Nothing was added for symmetry or completeness: an operation nobody
// calls is an operation every future fake has to implement for nothing.
//
// Those three - enumValues, readMultiValue and the vector overload of
// writeMultiValue - are here because a rollback has to put back exactly what it
// displaced, which is a strictly larger set of operations than applying the
// change needed. install() only ever writes a REG_MULTI_SZ that was absent, but
// uninstall() deletes the processing-mode values it wrote, and undoing that
// delete means reading a REG_MULTI_SZ and writing all of its strings back.
// deleteKey takes a key's values with it, so undoing one means enumerating them
// first. A port that could only describe the forward direction would leave the
// rollback guessing.
//
// WindowsRegistry keeps the live adapter operations that expose Win32 handles
// (openKey) or registry export syntax (formatExportHeader). GUID formatting,
// registry paths, the Windows version probe, and file access checks live in
// their own vocabulary or platform modules.
//
// CONTRACT - read this before writing a fake, because the real implementation's
// failure behaviour is what the install and uninstall code is built around.
//
//  * Keys are full paths whose first component is the textual root name, e.g.
//    L"HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO". The root is matched
//    case-insensitively. A path with no backslash, or an unrecognized root,
//    is an error, not a miss. All access goes through the 64-bit registry view.
//
//  * Errors are reported by throwing RegistryError (declared in
//    services/registry/RegistryError.h). It does not
//    derive from std::exception, so catch(...) or catch(const RegistryError&)
//    are the only things that will stop it.
//
//  * A missing key is an exception, not an empty result, for every operation
//    except keyExists. That includes valueExists: it answers false only when the
//    key exists and the value does not. install() relies on this asymmetry -
//    it guards FxProperties reads with keyExists first, then uses valueExists
//    inside.
//
//  * Reads are type-checked. readValue demands REG_SZ, readDWORDValue demands
//    REG_DWORD, readBinaryValue demands REG_BINARY; the wrong type throws rather
//    than converting.
//
//  * Writes never create the key they write into. createKey has to have run
//    first, or the write throws.
//
//  * deleteKey deletes one key and fails if that key still has subkeys. This is
//    not a detail: on Windows 24H2 the OS started putting its own subkeys under
//    FxProperties, which is why uninstall() now guards every whole-key delete
//    with keyEmpty (issue #189). A fake that lets deleteKey succeed on a
//    populated key would report that guard as dead code.
//
//  * createKey succeeds on an already existing key, and throws when the ACL
//    denies write access. install() catches exactly that to decide it must call
//    takeOwnership and makeWritable before retrying.
class IRegistry
{
public:
	virtual ~IRegistry() = default;

	// Reads. const marks them as leaving the registry as they found it, so the
	// port alone tells a reader which half of the interface can change a machine.

	// Throws if the key is missing or the value is not REG_SZ.
	virtual std::wstring readValue(const std::wstring& key, const std::wstring& valuename) const = 0;
	// Throws if the key is missing or the value is not REG_DWORD.
	virtual unsigned long readDWORDValue(const std::wstring& key, const std::wstring& valuename) const = 0;
	// The strings of a REG_MULTI_SZ. Throws if the key is missing or the value is
	// not REG_MULTI_SZ.
	virtual std::vector<std::wstring> readMultiValue(const std::wstring& key, const std::wstring& valuename) const = 0;
	// Raw bytes as stored. Throws if the key is missing or the value is not REG_BINARY.
	virtual std::vector<unsigned char> readBinaryValue(const std::wstring& key, const std::wstring& valuename) const = 0;
	// Immediate child key names, not full paths. Throws if the key is missing.
	virtual std::vector<std::wstring> enumSubKeys(const std::wstring& key) const = 0;
	// Value names of this key alone, in no guaranteed order. The default value of
	// a key comes back as an empty name, the way RegEnumValueW reports it.
	// Throws if the key is missing.
	virtual std::vector<std::wstring> enumValues(const std::wstring& key) const = 0;
	// The only operation that answers false instead of throwing for a missing key.
	virtual bool keyExists(const std::wstring& key) const = 0;
	// Throws if the key is missing; returns false only for a missing value.
	virtual bool valueExists(const std::wstring& key, const std::wstring& valuename) const = 0;
	// True when the key has neither subkeys nor values. Throws if the key is missing.
	virtual bool keyEmpty(const std::wstring& key) const = 0;

	// Writes. Every one of these requires the key to exist already.

	// Writes REG_SZ, overwriting whatever type was there.
	virtual void writeValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) = 0;
	virtual void writeDWORDValue(const std::wstring& key, const std::wstring& valuename, unsigned long value) = 0;
	// Writes a REG_MULTI_SZ holding this one string. The device layer only ever
	// writes the single processing-mode GUID through this one.
	virtual void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) = 0;
	// Writes a REG_MULTI_SZ holding every string given. Only a rollback uses it,
	// to put back a driver's multi-string value verbatim.
	virtual void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::vector<std::wstring>& values) = 0;
	// Throws if the value is not there, so callers guard it with valueExists.
	virtual void deleteValue(const std::wstring& key, const std::wstring& valuename) = 0;

	// Key lifetime.

	// Creates the whole missing path; succeeds if the key already exists.
	virtual void createKey(const std::wstring& key) = 0;
	// One key only, and only when it has no subkeys left.
	virtual void deleteKey(const std::wstring& key) = 0;

	// Permissions. Both rewrite the key's security descriptor, so install() calls
	// them only in the recovery path, after createKey has already thrown.

	// Makes the Administrators group the owner, enabling SE_TAKE_OWNERSHIP first.
	virtual void takeOwnership(const std::wstring& key) = 0;
	// Grants the Administrators group KEY_ALL_ACCESS, inherited by subkeys.
	virtual void makeWritable(const std::wstring& key) = 0;

	// Export. Reads the named values and writes them out as a .reg file, so it
	// is half a registry read and half a file write. It is in the port anyway:
	// it is the one registry access in install()'s backup branch that the other
	// fifteen operations do not cover, so leaving it out would mean a fake
	// registry could not cover install() at all - that single call would still
	// reach the real HKLM and throw. It is not const because writing the file is
	// a real effect, and the port should not claim otherwise.
	//
	// Every listed value is read with readValue, so a missing value or a
	// non-REG_SZ value throws. Throws if the file cannot be opened; it does not
	// create the containing directory.
	virtual void saveToFile(const std::wstring& key, const std::vector<std::wstring>& valuenames, const std::wstring& filepath) = 0;
};

// The adapter over the live Windows registry. One instance is enough for the
// whole process, and it is a function-local static so that its construction is
// ordered by first use rather than by translation-unit order - the device code
// is reached from an APO DLL's COM activation, which can run before any
// namespace-scope object of this DLL would have been constructed.
IRegistry& systemRegistry();
