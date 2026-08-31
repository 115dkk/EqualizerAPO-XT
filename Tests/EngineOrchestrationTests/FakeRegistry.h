/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	An in-memory IRegistry for the device tests. It exists so install(),
	load() and uninstall() can be exercised without touching HKLM: those three
	functions are the ones that rewrite an audio endpoint's APO chain, and a
	test that ran them for real would leave the machine's audio graph in
	whatever state the assertion failed in.

	FIDELITY IS THE WHOLE POINT. The device code is built around the real
	implementation's *failure* behaviour - install() decides it must take
	ownership because createKey threw, uninstall() keeps a key because
	deleteKey would have thrown - so a fake that is merely convenient would
	report those branches as untestable or, worse, as dead. Every rule below is
	taken from services/registry/WindowsRegistry.cpp and repeated here on purpose:

	  * A malformed path (no backslash, unknown root) is an error even for
	    keyExists, because the real keyExists calls splitKey first.
	  * A missing key throws in every operation except keyExists. valueExists
	    throws for a missing key and answers false only for a missing value.
	  * Reads are type-checked; REG_SZ is not converted from REG_DWORD.
	  * Writes never create their key.
	  * createKey creates the whole missing path and succeeds on an existing
	    key.
	  * deleteKey removes one key, takes its values with it, and refuses when
	    the key still has subkeys (RegDeleteKeyExW's behaviour, and the reason
	    uninstall() guards with keyEmpty - issue #189).

	Two deliberate departures, both because the fake is a registry and not a
	machine:

	  * takeOwnership and makeWritable cannot change an ACL that does not
	    exist, so they record the key they were called with. That record is
	    itself worth asserting: it is how a test sees that install() tried to
	    take ownership of the endpoint key. makeWritable additionally lifts a
	    denial armed by denyCreateKey(), which is how the recovery path is
	    driven.
	  * saveToFile performs the reads (so a missing or non-REG_SZ value still
	    throws, as it would in production) but records the export instead of
	    writing a .reg file, so the suite leaves nothing behind on disk. The one
	    divergence is the failure case: the real function opens the file before
	    reading, so a failed read leaves a truncated file behind, while the fake
	    records nothing.

	  * failValueWrite() arms a write failure on one value. Nothing in the
	    registry behaves that way on request; it is here because the transaction
	    that install() now runs inside can only be judged by interrupting an
	    install midway, and no other means of interrupting one exists.

	  * denyRead() arms a key that exists but cannot be opened, which is what a
	    driver-locked FxProperties key is. keyExists keeps answering true for it,
	    because the real keyExists opens the key for query and a denial is not an
	    absence; code that walks every endpoint has to survive the difference.

	enumValues returns the names in the map's case-insensitive order rather than
	the insertion order RegEnumValueW happens to produce. The port documents no
	order for that reason, and a caller that depends on one is relying on
	something the real registry does not promise either.

	Key paths and value names compare case-insensitively, which is what the
	Windows registry does. The device code always builds its paths from the
	constants in devices/DeviceAPOInfoKeys.h, so this never actually matters -
	it is here so that the fake cannot be stricter than the thing it stands in
	for.
*/

#pragma once

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "services/registry/IRegistry.h"
// For RegistryError: the port throws it, and callers up to and including
// DeviceAPOInfo::load catch it by that type.
#include "services/registry/WindowsRegistry.h"

namespace test
{

class FakeRegistry : public IRegistry
{
public:
	// The stored form of one value. The type is kept separately from the
	// payload so a read of the wrong type can throw the way the real one does
	// instead of silently converting.
	struct Value
	{
		enum class Type
		{
			String,
			Dword,
			Binary,
			MultiString
		};

		Type type = Type::String;
		std::wstring stringValue;
		unsigned long dwordValue = 0;
		std::vector<unsigned char> binaryValue;
		std::vector<std::wstring> multiValue;
	};

	// One recorded saveToFile call, with the values as they read at the time.
	struct Export
	{
		std::wstring key;
		std::wstring filePath;
		std::vector<std::wstring> valueNames;
		std::vector<std::wstring> values;
	};

	// Ordering that matches the registry's own case-insensitive name
	// comparison. Used for both key paths and value names.
	struct CaseInsensitiveLess
	{
		bool operator()(const std::wstring& left, const std::wstring& right) const
		{
			return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
				[](wchar_t leftChar, wchar_t rightChar) {
					return std::towlower(leftChar) < std::towlower(rightChar);
				});
		}
	};

	using ValueMap = std::map<std::wstring, Value, CaseInsensitiveLess>;

	// --- Seeding. Creates the key path on the way in, because a test that has
	// to create every parent key by hand before writing one value stops being
	// readable. The IRegistry writes deliberately do not do this.

	void seedKey(const std::wstring& key)
	{
		ensureKey(key);
	}

	void seedString(const std::wstring& key, const std::wstring& valuename, const std::wstring& value)
	{
		Value stored;
		stored.type = Value::Type::String;
		stored.stringValue = value;
		ensureKey(key)[valuename] = stored;
	}

	void seedDword(const std::wstring& key, const std::wstring& valuename, unsigned long value)
	{
		Value stored;
		stored.type = Value::Type::Dword;
		stored.dwordValue = value;
		ensureKey(key)[valuename] = stored;
	}

	void seedBinary(const std::wstring& key, const std::wstring& valuename, const std::vector<unsigned char>& value)
	{
		Value stored;
		stored.type = Value::Type::Binary;
		stored.binaryValue = value;
		ensureKey(key)[valuename] = stored;
	}

	void seedMulti(const std::wstring& key, const std::wstring& valuename, const std::vector<std::wstring>& value)
	{
		Value stored;
		stored.type = Value::Type::MultiString;
		stored.multiValue = value;
		ensureKey(key)[valuename] = stored;
	}

	// Arms the ACL denial install() recovers from: createKey on this exact path
	// throws until makeWritable is called on it or on one of its parents.
	void denyCreateKey(const std::wstring& key)
	{
		deniedCreateKeys_.insert(key);
	}

	// Arms a failure on one value's writes and deletes, so a test can stop an
	// install midway and see what the rollback puts back. A real machine fails
	// there for reasons a fake cannot reproduce - an ACL on that one property, a
	// driver holding the key - and "it never fails in the middle" is exactly the
	// assumption the transaction exists to remove, so the fake has to be able to
	// stage it. Reads are left working: the code being tested has to be able to
	// see the value it cannot write.
	void failValueWrite(const std::wstring& key, const std::wstring& valuename)
	{
		failedValueWrites_.insert(key + L"\\" + valuename);
	}

	// Makes one key refuse to be opened at all, the way a driver-locked
	// FxProperties key does. keyExists still answers true, because the real
	// keyExists opens with KEY_QUERY_VALUE and a key whose ACL denies the caller
	// is not a key that is absent - telling those two apart is the difference
	// between "install here" and "cannot install here", and code that walks
	// endpoints has to survive the second.
	void denyRead(const std::wstring& key)
	{
		deniedReadKeys_.insert(key);
	}

	// --- Inspection of the operations that have no in-memory effect.

	const std::vector<std::wstring>& takeOwnershipCalls() const
	{
		return takeOwnershipCalls_;
	}

	const std::vector<std::wstring>& makeWritableCalls() const
	{
		return makeWritableCalls_;
	}

	const std::vector<Export>& exports() const
	{
		return exports_;
	}

	// --- IRegistry.

	std::wstring readValue(const std::wstring& key, const std::wstring& valuename) const override
	{
		const Value& value = requireValue(key, valuename);
		if (value.type != Value::Type::String)
			throw RegistryError(L"Registry value " + key + L"\\" + valuename + L" has wrong type");
		return value.stringValue;
	}

	unsigned long readDWORDValue(const std::wstring& key, const std::wstring& valuename) const override
	{
		const Value& value = requireValue(key, valuename);
		if (value.type != Value::Type::Dword)
			throw RegistryError(L"Registry value " + key + L"\\" + valuename + L" has wrong type");
		return value.dwordValue;
	}

	std::vector<std::wstring> readMultiValue(const std::wstring& key, const std::wstring& valuename) const override
	{
		const Value& value = requireValue(key, valuename);
		if (value.type != Value::Type::MultiString)
			throw RegistryError(L"Registry value " + key + L"\\" + valuename + L" has wrong type");
		return value.multiValue;
	}

	std::vector<unsigned char> readBinaryValue(const std::wstring& key, const std::wstring& valuename) const override
	{
		const Value& value = requireValue(key, valuename);
		if (value.type != Value::Type::Binary)
			throw RegistryError(L"Registry value " + key + L"\\" + valuename + L" has wrong type");
		return value.binaryValue;
	}

	std::vector<std::wstring> enumSubKeys(const std::wstring& key) const override
	{
		requireKey(key);

		std::vector<std::wstring> result;
		const std::wstring prefix = key + L"\\";
		for (const auto& entry : keys_)
		{
			if (!startsWith(entry.first, prefix))
				continue;

			const std::wstring remainder = entry.first.substr(prefix.size());
			const size_t separator = remainder.find(L'\\');
			const std::wstring child = separator == std::wstring::npos ? remainder : remainder.substr(0, separator);
			// A grandchild key contributes its parent's name, which the parent
			// already contributed, so drop the repeat.
			if (std::find_if(result.begin(), result.end(), [&child](const std::wstring& seen) {
					return equalNames(seen, child);
				}) == result.end())
			{
				result.push_back(child);
			}
		}

		return result;
	}

	std::vector<std::wstring> enumValues(const std::wstring& key) const override
	{
		const ValueMap& values = requireKey(key);

		std::vector<std::wstring> result;
		for (const auto& entry : values)
			result.push_back(entry.first);
		return result;
	}

	bool keyExists(const std::wstring& key) const override
	{
		// Not requireKey: this is the one operation that answers instead of
		// throwing. The format check stays, because the real one runs splitKey
		// before it ever looks for the key.
		requireWellFormed(key);
		return keys_.contains(key);
	}

	bool valueExists(const std::wstring& key, const std::wstring& valuename) const override
	{
		const ValueMap& values = requireKey(key);
		return values.contains(valuename);
	}

	bool keyEmpty(const std::wstring& key) const override
	{
		const ValueMap& values = requireKey(key);
		return values.empty() && !hasSubKeys(key);
	}

	void writeValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) override
	{
		Value stored;
		stored.type = Value::Type::String;
		stored.stringValue = value;
		store(key, valuename, stored);
	}

	void writeDWORDValue(const std::wstring& key, const std::wstring& valuename, unsigned long value) override
	{
		Value stored;
		stored.type = Value::Type::Dword;
		stored.dwordValue = value;
		store(key, valuename, stored);
	}

	void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::wstring& value) override
	{
		writeMultiValue(key, valuename, std::vector<std::wstring>{value});
	}

	void writeMultiValue(const std::wstring& key, const std::wstring& valuename, const std::vector<std::wstring>& values) override
	{
		Value stored;
		stored.type = Value::Type::MultiString;
		stored.multiValue = values;
		store(key, valuename, stored);
	}

	void deleteValue(const std::wstring& key, const std::wstring& valuename) override
	{
		ValueMap& values = requireWritableKey(key);
		requireWritableValue(key, valuename);
		ValueMap::iterator it = values.find(valuename);
		if (it == values.end())
			throw RegistryError(L"Error while deleting registry value " + key + L"\\" + valuename + L": not found");
		values.erase(it);
	}

	void createKey(const std::wstring& key) override
	{
		requireWellFormed(key);
		if (deniedCreateKeys_.contains(key))
			throw RegistryError(L"Error while creating registry key " + key + L": access is denied");
		ensureKey(key);
	}

	void deleteKey(const std::wstring& key) override
	{
		requireKey(key);
		// RegDeleteKeyExW refuses a key that still has subkeys. Windows 24H2
		// started creating subkeys under FxProperties, which turned this into a
		// live uninstall failure (issue #189), so the fake has to refuse too or
		// the keyEmpty guard in uninstall() would look pointless.
		if (hasSubKeys(key))
			throw RegistryError(L"Error while deleting registry key " + key + L": the key has subkeys");
		keys_.erase(key);
	}

	void takeOwnership(const std::wstring& key) override
	{
		requireKey(key);
		takeOwnershipCalls_.push_back(key);
	}

	void makeWritable(const std::wstring& key) override
	{
		requireKey(key);
		makeWritableCalls_.push_back(key);

		// Granting write access to a key grants it to the subtree, so a denial
		// armed anywhere below this key is lifted.
		const std::wstring prefix = key + L"\\";
		for (std::set<std::wstring, CaseInsensitiveLess>::iterator it = deniedCreateKeys_.begin(); it != deniedCreateKeys_.end();)
		{
			if (equalNames(*it, key) || startsWith(*it, prefix))
				it = deniedCreateKeys_.erase(it);
			else
				++it;
		}
	}

	void saveToFile(const std::wstring& key, const std::vector<std::wstring>& valuenames, const std::wstring& filepath) override
	{
		Export exported;
		exported.key = key;
		exported.filePath = filepath;
		exported.valueNames = valuenames;
		for (const std::wstring& valuename : valuenames)
			exported.values.push_back(readValue(key, valuename));
		exports_.push_back(exported);
	}

private:
	static bool equalNames(const std::wstring& left, const std::wstring& right)
	{
		CaseInsensitiveLess less;
		return !less(left, right) && !less(right, left);
	}

	static bool startsWith(const std::wstring& text, const std::wstring& prefix)
	{
		return text.size() > prefix.size() && equalNames(text.substr(0, prefix.size()), prefix);
	}

	// Mirrors WindowsRegistry::splitKey: the root has to be there and has to be
	// one of the five, or it is a format error rather than a miss.
	static void requireWellFormed(const std::wstring& key)
	{
		const size_t separator = key.find(L'\\');
		if (separator == std::wstring::npos)
			throw RegistryError(L"Key " + key + L" has invalid format");

		const std::wstring root = key.substr(0, separator);
		for (const wchar_t* known : {L"HKEY_CLASSES_ROOT", L"HKEY_CURRENT_CONFIG", L"HKEY_CURRENT_USER",
				L"HKEY_LOCAL_MACHINE", L"HKEY_USERS"})
		{
			if (equalNames(root, known))
				return;
		}

		throw RegistryError(L"Unknown root key " + root);
	}

	const ValueMap& requireKey(const std::wstring& key) const
	{
		requireWellFormed(key);
		std::map<std::wstring, ValueMap, CaseInsensitiveLess>::const_iterator it = keys_.find(key);
		if (it == keys_.end())
			throw RegistryError(L"Error while opening registry key " + key + L": the system cannot find the file specified");
		if (deniedReadKeys_.contains(key))
			throw RegistryError(L"Error while opening registry key " + key + L": access is denied");
		return it->second;
	}

	// The one write path, so the armed-failure check cannot be forgotten by a
	// future write overload.
	void store(const std::wstring& key, const std::wstring& valuename, const Value& value)
	{
		ValueMap& values = requireWritableKey(key);
		requireWritableValue(key, valuename);
		values[valuename] = value;
	}

	void requireWritableValue(const std::wstring& key, const std::wstring& valuename) const
	{
		if (failedValueWrites_.contains(key + L"\\" + valuename))
			throw RegistryError(L"Error while writing registry value " + key + L"\\" + valuename + L": access is denied");
	}

	// The write-side counterpart. Separate only so the const reads cannot reach
	// a mutable map by accident.
	ValueMap& requireWritableKey(const std::wstring& key)
	{
		requireWellFormed(key);
		std::map<std::wstring, ValueMap, CaseInsensitiveLess>::iterator it = keys_.find(key);
		if (it == keys_.end())
			throw RegistryError(L"Error while opening registry key " + key + L": the system cannot find the file specified");
		return it->second;
	}

	// RegCreateKeyExW creates every missing key along the path, so every prefix
	// that still contains a backslash becomes a key of its own.
	ValueMap& ensureKey(const std::wstring& key)
	{
		requireWellFormed(key);
		for (size_t position = key.find(L'\\', key.find(L'\\') + 1); position != std::wstring::npos;
			position = key.find(L'\\', position + 1))
		{
			keys_.emplace(key.substr(0, position), ValueMap());
		}
		return keys_.emplace(key, ValueMap()).first->second;
	}

	const Value& requireValue(const std::wstring& key, const std::wstring& valuename) const
	{
		const ValueMap& values = requireKey(key);
		ValueMap::const_iterator it = values.find(valuename);
		if (it == values.end())
			throw RegistryError(L"Error while reading registry value " + key + L"\\" + valuename + L": not found");
		return it->second;
	}

	bool hasSubKeys(const std::wstring& key) const
	{
		const std::wstring prefix = key + L"\\";
		for (const auto& entry : keys_)
		{
			if (startsWith(entry.first, prefix))
				return true;
		}
		return false;
	}

	std::map<std::wstring, ValueMap, CaseInsensitiveLess> keys_;
	std::set<std::wstring, CaseInsensitiveLess> deniedCreateKeys_;
	std::set<std::wstring, CaseInsensitiveLess> failedValueWrites_;
	std::set<std::wstring, CaseInsensitiveLess> deniedReadKeys_;
	std::vector<std::wstring> takeOwnershipCalls_;
	std::vector<std::wstring> makeWritableCalls_;
	std::vector<Export> exports_;
};

} // namespace test
