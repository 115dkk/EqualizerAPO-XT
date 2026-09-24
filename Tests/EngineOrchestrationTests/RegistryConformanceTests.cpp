/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	One battery of IRegistry rules, run over FakeRegistry and over the real
	registry (audit #348 F25/TD-38).

	The device tests trust FakeRegistry to fail where the real registry fails:
	install() takes ownership because createKey threw, uninstall() keeps a key
	because deleteKey would have thrown. The fake's header lists those rules as
	taken from WindowsRegistry.cpp, but nothing compared the two, and the list
	had already drifted: it said keyExists answers true for a key that refuses
	to be opened, while the real one answered false. Each rule is stated once
	here, against the port, and both implementations have to pass it.

	The real run works under HKCU\Software\EqualizerAPO-XT-Tests\{random GUID},
	the sandbox the ConfigWatcher test uses, and removes it on every path. It
	needs no elevation and touches nothing outside that key and one file in
	%TEMP%.

	Not covered, because the fake departs from the real registry on purpose
	(FakeRegistry.h lists why): takeOwnership, makeWritable, the armed write
	failures, and what saveToFile leaves on disk.
*/

#include <algorithm>
#include <cwctype>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <sddl.h>

#include "services/registry/WindowsRegistry.h"
#include "Tests/TestHarness.h"

#include "Tests/FakeRegistry.h"

namespace
{
using test::FakeRegistry;

// The registry under test and the few things the port cannot do for itself.
struct RegistryUnderTest
{
	using Lock = std::function<void(const std::wstring& key)>;
	using SeedBinary = std::function<void(const std::wstring& key, const std::wstring& name, const std::vector<unsigned char>& bytes)>;

	RegistryUnderTest(std::string label, IRegistry& registry, std::wstring root, Lock lock, SeedBinary seedBinary,
		std::wstring exportPath)
		: label(std::move(label)),
		registry(registry),
		root(std::move(root)),
		lock(std::move(lock)),
		seedBinary(std::move(seedBinary)),
		exportPath(std::move(exportPath))
	{
	}

	std::string label;
	IRegistry& registry;
	// An existing, empty key. Everything the battery creates is below it.
	std::wstring root;
	// Makes an existing key refuse every access the port asks for.
	Lock lock;
	// Stores a REG_BINARY, which the port reads but has no reason to write.
	SeedBinary seedBinary;
	// A path saveToFile may create.
	std::wstring exportPath;
};

template<typename Operation>
bool throwsRegistryError(Operation&& operation)
{
	try
	{
		operation();
	}
	catch (const RegistryError&)
	{
		return true;
	}
	return false;
}

std::wstring lowered(std::wstring text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
	return text;
}

// Neither implementation promises an enumeration order, and names compare
// case-insensitively, so compare them as a lower-cased sorted set.
std::vector<std::wstring> normalizedNames(std::vector<std::wstring> names)
{
	for (std::wstring& name : names)
		name = lowered(name);
	std::sort(names.begin(), names.end());
	return names;
}

void checkPaths(test::Harness& harness, const RegistryUnderTest& under)
{
	const std::string at = under.label + ": ";
	IRegistry& registry = under.registry;

	harness.expect(throwsRegistryError([&] { registry.keyExists(L"NoBackslash"); }),
		at + "a path without a backslash is an error, even for keyExists");
	harness.expect(throwsRegistryError([&] { registry.keyExists(L"HKEY_NOWHERE\\Software"); }),
		at + "an unknown root is an error, even for keyExists");
	harness.expectTrue(registry.keyExists(under.root), at + "the sandbox root exists");
	harness.expectTrue(registry.keyExists(lowered(under.root)),
		at + "key paths, root included, compare case-insensitively");
}

void checkMissingKey(test::Harness& harness, const RegistryUnderTest& under)
{
	const std::string at = under.label + ": ";
	IRegistry& registry = under.registry;
	const std::wstring missing = under.root + L"\\Missing";

	harness.expectFalse(registry.keyExists(missing), at + "keyExists answers false for a missing key");

	struct Operation
	{
		const char* name = nullptr;
		std::function<void()> run;
	};
	const Operation operations[] = {
		{"readValue", [&] { registry.readValue(missing, L"Name"); }},
		{"readDWORDValue", [&] { registry.readDWORDValue(missing, L"Name"); }},
		{"readMultiValue", [&] { registry.readMultiValue(missing, L"Name"); }},
		{"readBinaryValue", [&] { registry.readBinaryValue(missing, L"Name"); }},
		{"enumSubKeys", [&] { registry.enumSubKeys(missing); }},
		{"enumValues", [&] { registry.enumValues(missing); }},
		{"valueExists", [&] { registry.valueExists(missing, L"Name"); }},
		{"keyEmpty", [&] { registry.keyEmpty(missing); }},
		{"writeValue", [&] { registry.writeValue(missing, L"Name", L"text"); }},
		{"writeDWORDValue", [&] { registry.writeDWORDValue(missing, L"Name", 1); }},
		{"writeMultiValue (one string)", [&] { registry.writeMultiValue(missing, L"Name", L"text"); }},
		{"writeMultiValue (list)", [&] { registry.writeMultiValue(missing, L"Name", std::vector<std::wstring>{L"a", L"b"}); }},
		{"deleteValue", [&] { registry.deleteValue(missing, L"Name"); }},
		{"deleteKey", [&] { registry.deleteKey(missing); }},
	};
	for (const Operation& operation : operations)
	{
		harness.expect(throwsRegistryError(operation.run),
			at + operation.name + " throws for a missing key");
	}

	harness.expectFalse(registry.keyExists(missing), at + "a write never creates the key it writes into");
}

void checkCreateKey(test::Harness& harness, const RegistryUnderTest& under)
{
	const std::string at = under.label + ": ";
	IRegistry& registry = under.registry;
	const std::wstring top = under.root + L"\\Created";

	registry.createKey(top + L"\\Middle\\Leaf");
	harness.expect(registry.keyExists(top) && registry.keyExists(top + L"\\Middle") && registry.keyExists(top + L"\\Middle\\Leaf"),
		at + "createKey creates every missing key on the path");

	registry.writeValue(top, L"Kept", L"value");
	harness.expect(!throwsRegistryError([&] { registry.createKey(top); }),
		at + "createKey succeeds on an existing key");
	harness.expect(registry.readValue(top, L"Kept") == L"value", at + "createKey leaves an existing key's values alone");
}

void checkValues(test::Harness& harness, const RegistryUnderTest& under)
{
	const std::string at = under.label + ": ";
	IRegistry& registry = under.registry;
	const std::wstring key = under.root + L"\\Values";

	registry.createKey(key);
	harness.expectTrue(registry.keyEmpty(key), at + "a new key is empty");
	harness.expectFalse(registry.valueExists(key, L"Absent"), at + "valueExists answers false for a missing value");
	harness.expect(throwsRegistryError([&] { registry.readValue(key, L"Absent"); }), at + "reading a missing value throws");
	harness.expect(throwsRegistryError([&] { registry.deleteValue(key, L"Absent"); }), at + "deleting a missing value throws");

	registry.writeValue(key, L"Text", L"hello");
	registry.writeValue(key, L"Blank", L"");
	registry.writeDWORDValue(key, L"Number", 0x12345678);
	registry.writeMultiValue(key, L"List", std::vector<std::wstring>{L"a", L"", L"b"});
	registry.writeMultiValue(key, L"Trailing", std::vector<std::wstring>{L"a", L""});
	registry.writeMultiValue(key, L"None", std::vector<std::wstring>{});
	registry.writeMultiValue(key, L"Single", L"one");
	registry.writeMultiValue(key, L"SingleBlank", L"");
	under.seedBinary(key, L"Bytes", {0x00, 0x01, 0x7F, 0xFF});
	registry.writeValue(key, L"", L"default");

	harness.expectFalse(registry.keyEmpty(key), at + "a key with a value is not empty");
	harness.expectTrue(registry.valueExists(key, L"Text"), at + "valueExists answers true for a written value");
	harness.expect(registry.readValue(key, L"Text") == L"hello", at + "a REG_SZ reads back as written");
	harness.expect(registry.readValue(key, L"TEXT") == L"hello", at + "value names compare case-insensitively");
	harness.expect(registry.readValue(key, L"Blank").empty(), at + "an empty REG_SZ reads back empty");
	harness.expectEqual(registry.readDWORDValue(key, L"Number"), 0x12345678ul, at + "a REG_DWORD reads back as written");
	harness.expect(registry.readMultiValue(key, L"List") == std::vector<std::wstring>({L"a", L"", L"b"}),
		at + "an empty string between two others survives a REG_MULTI_SZ round trip");
	harness.expect(registry.readMultiValue(key, L"Trailing") == std::vector<std::wstring>({L"a"}),
		at + "a trailing empty string does not survive, it is the list terminator");
	harness.expect(registry.readMultiValue(key, L"None").empty(), at + "an empty REG_MULTI_SZ reads back empty");
	harness.expect(registry.readMultiValue(key, L"Single") == std::vector<std::wstring>({L"one"}),
		at + "the one-string writeMultiValue stores a one-element list");
	harness.expect(registry.readMultiValue(key, L"SingleBlank").empty(),
		at + "a one-string writeMultiValue of the empty string reads back as an empty list");
	harness.expect(registry.readBinaryValue(key, L"Bytes") == std::vector<unsigned char>({0x00, 0x01, 0x7F, 0xFF}),
		at + "a REG_BINARY reads back byte for byte");
	harness.expect(registry.readValue(key, L"") == L"default", at + "the default value is the empty name");

	harness.expect(throwsRegistryError([&] { registry.readValue(key, L"Number"); }), at + "readValue refuses a REG_DWORD");
	harness.expect(throwsRegistryError([&] { registry.readValue(key, L"List"); }), at + "readValue refuses a REG_MULTI_SZ");
	harness.expect(throwsRegistryError([&] { registry.readValue(key, L"Bytes"); }), at + "readValue refuses a REG_BINARY");
	harness.expect(throwsRegistryError([&] { registry.readDWORDValue(key, L"Text"); }), at + "readDWORDValue refuses a REG_SZ");
	harness.expect(throwsRegistryError([&] { registry.readMultiValue(key, L"Text"); }), at + "readMultiValue refuses a REG_SZ");
	harness.expect(throwsRegistryError([&] { registry.readBinaryValue(key, L"Text"); }), at + "readBinaryValue refuses a REG_SZ");

	harness.expect(normalizedNames(registry.enumValues(key)) == normalizedNames({L"", L"Blank", L"Bytes", L"List", L"None",
		L"Number", L"Single", L"SingleBlank", L"Text", L"Trailing"}),
		at + "enumValues lists every value by name, the default value as the empty name");

	registry.deleteValue(key, L"Text");
	harness.expectFalse(registry.valueExists(key, L"Text"), at + "deleteValue removes the value");

	harness.expect(throwsRegistryError([&] { registry.saveToFile(key, {L"Absent"}, under.exportPath); }),
		at + "saveToFile throws for a missing value");
	harness.expect(throwsRegistryError([&] { registry.saveToFile(key, {L"Number"}, under.exportPath); }),
		at + "saveToFile throws for a value that is not REG_SZ");
}

void checkSubKeys(test::Harness& harness, const RegistryUnderTest& under)
{
	const std::string at = under.label + ": ";
	IRegistry& registry = under.registry;
	const std::wstring tree = under.root + L"\\Tree";

	registry.createKey(tree + L"\\First\\Grandchild");
	registry.createKey(tree + L"\\Second");

	harness.expect(normalizedNames(registry.enumSubKeys(tree)) == normalizedNames({L"First", L"Second"}),
		at + "enumSubKeys lists the immediate children by name, without paths or grandchildren");
	harness.expect(registry.enumValues(tree).empty(), at + "a key with only subkeys has no values");
	harness.expectFalse(registry.keyEmpty(tree), at + "a key with only subkeys is not empty");

	harness.expect(throwsRegistryError([&] { registry.deleteKey(tree + L"\\First"); }),
		at + "deleteKey refuses a key that still has subkeys");
	harness.expectTrue(registry.keyExists(tree + L"\\First"), at + "the refused key is still there");

	registry.writeValue(tree + L"\\First\\Grandchild", L"Name", L"value");
	registry.deleteKey(tree + L"\\First\\Grandchild");
	harness.expectFalse(registry.keyExists(tree + L"\\First\\Grandchild"), at + "deleteKey removes a leaf and its values");
	harness.expect(!throwsRegistryError([&] { registry.deleteKey(tree + L"\\First"); }),
		at + "deleteKey succeeds once the subkeys are gone");
	harness.expectFalse(registry.keyExists(tree + L"\\First"), at + "the emptied key is gone");
}

// Audit #348: the real keyExists used to answer false here, and install()
// then treated a driver-locked FxProperties as missing.
void checkLockedKey(test::Harness& harness, const RegistryUnderTest& under)
{
	const std::string at = under.label + ": ";
	IRegistry& registry = under.registry;
	const std::wstring key = under.root + L"\\Locked";

	registry.createKey(key);
	registry.writeValue(key, L"Name", L"value");
	under.lock(key);

	harness.expectTrue(registry.keyExists(key), at + "a key that refuses to be opened still exists");
	harness.expect(throwsRegistryError([&] { registry.valueExists(key, L"Name"); }), at + "valueExists throws for a locked key");
	harness.expect(throwsRegistryError([&] { registry.readValue(key, L"Name"); }), at + "readValue throws for a locked key");
	harness.expect(throwsRegistryError([&] { registry.enumValues(key); }), at + "enumValues throws for a locked key");
	harness.expect(throwsRegistryError([&] { registry.enumSubKeys(key); }), at + "enumSubKeys throws for a locked key");
	harness.expect(throwsRegistryError([&] { registry.keyEmpty(key); }), at + "keyEmpty throws for a locked key");
	harness.expect(throwsRegistryError([&] { registry.createKey(key); }),
		at + "createKey throws for an existing key that denies write access");
}

void runBattery(test::Harness& harness, const RegistryUnderTest& under)
{
	checkPaths(harness, under);
	checkMissingKey(harness, under);
	checkCreateKey(harness, under);
	checkValues(harness, under);
	checkSubKeys(harness, under);
	checkLockedKey(harness, under);
}

std::wstring newGuidText()
{
	GUID guid = {};
	CoCreateGuid(&guid);
	wchar_t text[64] = {};
	StringFromGUID2(guid, text, 64);
	return text;
}

std::wstring currentUserSid()
{
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return std::wstring();

	std::wstring result;
	DWORD size = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &size);
	std::vector<unsigned char> buffer(size);
	wchar_t* sidText = nullptr;
	if (size != 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)
		&& ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sidText))
	{
		result = sidText;
		LocalFree(sidText);
	}
	CloseHandle(token);
	return result;
}

const std::wstring hkcuPrefix = L"HKEY_CURRENT_USER\\";
const wchar_t sandboxParent[] = L"Software\\EqualizerAPO-XT-Tests";

// HKCU\Software\EqualizerAPO-XT-Tests\{GUID} for one run, removed on every
// path by the destructor.
class Sandbox
{
public:
	Sandbox()
		: subKey_(std::wstring(sandboxParent) + L"\\" + newGuidText())
	{
		HKEY created = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey_.c_str(), 0, nullptr, 0,
			KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &created, nullptr) == ERROR_SUCCESS)
		{
			RegCloseKey(created);
			created_ = true;
		}

		wchar_t tempPath[MAX_PATH] = {};
		const DWORD length = GetTempPathW(MAX_PATH, tempPath);
		exportPath_ = (length > 0 && length < MAX_PATH ? std::wstring(tempPath) : std::wstring(L".\\"))
			+ L"EqualizerAPO-XT-Tests-" + newGuidText() + L".reg";
	}

	~Sandbox()
	{
		// A locked key refuses the enumeration RegDeleteTreeW needs, but its
		// ACL keeps DELETE, so it goes first and by name.
		for (const std::wstring& locked : locked_)
			RegDeleteKeyExW(HKEY_CURRENT_USER, locked.c_str(), KEY_WOW64_64KEY, 0);
		RegDeleteTreeW(HKEY_CURRENT_USER, subKey_.c_str());
		RegDeleteKeyExW(HKEY_CURRENT_USER, subKey_.c_str(), KEY_WOW64_64KEY, 0);
		// Fails harmlessly while another run's sandbox key still exists.
		RegDeleteKeyExW(HKEY_CURRENT_USER, sandboxParent, KEY_WOW64_64KEY, 0);
		DeleteFileW(exportPath_.c_str());
	}

	Sandbox(const Sandbox&) = delete;
	Sandbox& operator=(const Sandbox&) = delete;

	bool created() const
	{
		return created_;
	}

	std::wstring root() const
	{
		return hkcuPrefix + subKey_;
	}

	const std::wstring& exportPath() const
	{
		return exportPath_;
	}

	// Replaces the key's DACL with one that grants the current user DELETE,
	// READ_CONTROL and WRITE_DAC only, so every query, enumeration and write
	// is denied while cleanup can still remove the key.
	void lock(const std::wstring& key)
	{
		const std::wstring subKey = key.substr(hkcuPrefix.size());
		const std::wstring sddl = L"D:P(A;;0x70000;;;" + currentUserSid() + L")";
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
			return;

		HKEY handle = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, WRITE_DAC | KEY_WOW64_64KEY, &handle) == ERROR_SUCCESS)
		{
			if (RegSetKeySecurity(handle, DACL_SECURITY_INFORMATION, descriptor) == ERROR_SUCCESS)
				locked_.push_back(subKey);
			RegCloseKey(handle);
		}
		LocalFree(descriptor);
	}

	static void seedBinary(const std::wstring& key, const std::wstring& name, const std::vector<unsigned char>& bytes)
	{
		RegSetKeyValueW(HKEY_CURRENT_USER, key.substr(hkcuPrefix.size()).c_str(), name.c_str(), REG_BINARY,
			bytes.data(), static_cast<DWORD>(bytes.size()));
	}

private:
	std::wstring subKey_;
	std::wstring exportPath_;
	std::vector<std::wstring> locked_;
	bool created_ = false;
};

void testFakeRegistryConforms(test::Harness& harness)
{
	FakeRegistry registry;
	const std::wstring root = hkcuPrefix + sandboxParent + L"\\Fake";
	registry.seedKey(root);

	const RegistryUnderTest under(
		"FakeRegistry",
		registry,
		root,
		[&registry](const std::wstring& key) {
			registry.denyRead(key);
			registry.denyCreateKey(key);
		},
		[&registry](const std::wstring& key, const std::wstring& name, const std::vector<unsigned char>& bytes) {
			registry.seedBinary(key, name, bytes);
		},
		L"unused.reg");
	runBattery(harness, under);
}

void testWindowsRegistryConforms(test::Harness& harness)
{
	Sandbox sandbox;
	if (!sandbox.created())
	{
		harness.expect(false, "could not create the HKCU sandbox key for the registry conformance battery");
		return;
	}

	const std::wstring root = sandbox.root();
	const std::wstring exportPath = sandbox.exportPath();
	const RegistryUnderTest under(
		"WindowsRegistry",
		systemRegistry(),
		root,
		[&sandbox](const std::wstring& key) { sandbox.lock(key); },
		&Sandbox::seedBinary,
		exportPath);
	runBattery(harness, under);
}
} // namespace

void runRegistryConformanceTests(test::Harness& harness)
{
	testFakeRegistryConforms(harness);
	testWindowsRegistryConforms(harness);
}
