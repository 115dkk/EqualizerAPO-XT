/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Tests for the policy that keeps configuration-referenced file opens on
	local drives while preserving same-share references for network configs.
	A path is judged by where it leads (audit #348 A1): the walk is tested on
	a file system given as a table, and once on real junctions.
*/

#include <cstdio>
#include <cstring>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "filters/ConfigPathPolicy.h"
#include "platform/windows/Win32Resource.h"
#include <winioctl.h>
#include "Tests/TestHarness.h"

using std::wstring;
using Kind = ConfigPathPolicy::Entry::Kind;

namespace
{
test::Harness harness("ConfigPathPolicyTests");

bool contains(const wstring& text, const wstring& part)
{
	return text.find(part) != wstring::npos;
}

void expectRoot(const wstring& path, const wstring& expected, const char* message)
{
	harness.expectTrue(ConfigPathPolicy::remoteRoot(path) == expected, message);
}

void testRemoteRoot()
{
	expectRoot(L"\\\\srv\\share\\x.txt", L"\\\\srv\\share", "UNC root keeps the server and share");
	expectRoot(L"//SRV/Share/x", L"\\\\srv\\share", "forward slashes are normalized and case-folded");
	expectRoot(L"\\\\?\\UNC\\srv\\share\\x", L"\\\\srv\\share", "verbatim UNC path has the root of the share it names");
	expectRoot(L"\\\\?\\unc\\SRV\\Share", L"\\\\srv\\share", "the UNC marker and the share are case-folded");
	expectRoot(L"\\\\.\\UNC\\srv\\share\\x", L"\\\\srv\\share", "device-namespace UNC path names the same share");
	expectRoot(L"\\??\\UNC\\srv\\share\\x", L"\\\\srv\\share", "NT-prefixed UNC path names the same share");
	expectRoot(L"\\\\?\\C:\\x", L"", "verbatim drive path is local");
	expectRoot(L"\\??\\C:\\x", L"", "NT-prefixed drive path is local");
	expectRoot(L"\\\\?\\Volume{0a1b2c3d-0000-0000-0000-000000000000}\\x", L"", "volume GUID path is local");
	expectRoot(L"\\\\?\\GLOBALROOT\\Device\\Mup\\srv\\share", L"\\\\?\\globalroot", "object-manager path has a device root");
	expectRoot(L"\\\\.\\pipe\\name", L"\\\\.\\pipe", "device pipe path has a device root");
	expectRoot(L"\\\\srv", L"\\\\srv", "single-component UNC path keeps its component");
	expectRoot(L"C:\\x", L"", "drive-letter absolute path is local");
	expectRoot(L"c:\\x", L"", "lower-case drive-letter path is local");
	expectRoot(L"x\\y", L"", "relative path is local");
	expectRoot(L"\\x", L"", "root-relative path is local");
	expectRoot(L"C:x", L"", "drive-relative path is local");
	expectRoot(L"", L"", "empty path has no remote root");
}

void expectAllowed(const wstring& path, const wstring& configPath, const char* message)
{
	wstring reason;
	harness.expectTrue(ConfigPathPolicy::allowsOpen(path, configPath, reason), message);
}

void expectRefused(const wstring& path, const wstring& configPath, const char* message)
{
	wstring reason;
	harness.expectFalse(ConfigPathPolicy::allowsOpen(path, configPath, reason), message);
	harness.expectFalse(reason.empty(), "a refused path carries a reason");
	harness.expectTrue(contains(reason, path), "the refusal reason contains the path");
	harness.expectTrue(contains(reason, L"network share"), "the refusal reason names a network share");
}

void testLocalConfigPolicy()
{
	const wstring configPath = L"C:\\cfg\\config.txt";
	expectAllowed(L"C:\\cfg\\a.txt", configPath, "same-drive absolute path is allowed");
	expectAllowed(L"D:\\ir\\a.wav", configPath, "other-drive absolute path is allowed");
	expectAllowed(L"sub\\a.txt", configPath, "relative path is allowed");
	expectAllowed(L"\\a.txt", configPath, "root-relative path is allowed");
	expectAllowed(L"C:a.txt", configPath, "drive-relative path is allowed");
	expectAllowed(L"\\\\?\\C:\\a.txt", configPath, "verbatim drive path is allowed: it is the same local file");
	expectRefused(L"\\\\srv\\share\\a.txt", configPath, "UNC path is refused for a local config");
	expectRefused(L"//srv/share/a.txt", configPath, "forward-slash UNC path is refused for a local config");
	expectRefused(L"\\\\.\\pipe\\x", configPath, "device path is refused for a local config");
	expectRefused(L"\\\\?\\UNC\\srv\\share\\a.txt", configPath, "verbatim UNC path is refused for a local config");
	expectRefused(L"\\??\\UNC\\srv\\share\\a.txt", configPath,
		"NT-prefixed UNC path is refused: std::filesystem keeps it absolute and CreateFileW opens it on the share");
	expectRefused(L"\\\\?\\GLOBALROOT\\Device\\Mup\\srv\\share\\a.txt", configPath,
		"object-manager path to the network redirector is refused");
}

void testSameShareExemption()
{
	const wstring configPath = L"\\\\srv\\share\\config.txt";
	expectAllowed(L"\\\\srv\\share\\sub\\a.txt", configPath, "same-share subpath is allowed");
	expectAllowed(L"\\\\SRV\\SHARE\\a.txt", configPath, "same-share comparison ignores case");
	expectAllowed(L"//srv/share/a.txt", configPath, "same-share comparison accepts forward slashes");
	expectAllowed(L"\\\\?\\UNC\\srv\\share\\a.txt", configPath, "a verbatim spelling of the same share is allowed");
	expectAllowed(L"\\\\srv\\share\\a.txt", L"\\\\?\\UNC\\SRV\\share\\config.txt",
		"a config spelled verbatim may name its share plainly");
	expectRefused(L"\\\\srv\\other\\a.txt", configPath, "different share is refused");
	expectRefused(L"\\\\other\\share\\a.txt", configPath, "different server is refused");
	expectRefused(L"\\\\?\\UNC\\srv\\other\\a.txt", configPath, "a verbatim spelling of another share is refused");
}

void testEmptyConfigPath()
{
	expectAllowed(L"C:\\a.txt", L"", "local path is allowed without a config path");
	expectRefused(L"\\\\srv\\share\\a.txt", L"", "remote path is refused without a config path");
}

wstring lowered(wstring text)
{
	for (wchar_t& character : text)
		character = static_cast<wchar_t>(std::towlower(character));
	return text;
}

// A file system given as a table. A path it does not list is a plain file
// or folder. `\\?\C:\x` and `C:\x` are the same entry, as they are on disk.
class TableFileSystem : public ConfigPathPolicy::FileSystem
{
public:
	void link(const wstring& path, const wstring& target, bool relative = false)
	{
		ConfigPathPolicy::Entry entry;
		entry.kind = Kind::Link;
		entry.target = target;
		entry.relative = relative;
		entries[key(path)] = entry;
	}

	void mark(const wstring& path, Kind kind)
	{
		ConfigPathPolicy::Entry entry;
		entry.kind = kind;
		entries[key(path)] = entry;
	}

	void arrives(const wstring& path, const wstring& finalPath)
	{
		finalPaths[key(path)] = finalPath;
	}

	ConfigPathPolicy::Entry entry(const wstring& path) const override
	{
		read.push_back(path);
		const auto found = entries.find(key(path));
		if (found != entries.end())
			return found->second;
		ConfigPathPolicy::Entry plain;
		plain.kind = Kind::Plain;
		return plain;
	}

	wstring finalPath(const wstring& path) const override
	{
		opened.push_back(path);
		const auto found = finalPaths.find(key(path));
		return found == finalPaths.end() ? L"" : found->second;
	}

	bool isNetworkDrive(wchar_t driveLetter) const override
	{
		return networkDrives.count(static_cast<wchar_t>(std::towupper(driveLetter))) != 0;
	}

	bool readAnything(const wstring& part) const
	{
		for (const wstring& path : read)
		{
			if (contains(lowered(path), lowered(part)))
				return true;
		}
		return false;
	}

	std::set<wchar_t> networkDrives;
	mutable std::vector<wstring> read;
	mutable std::vector<wstring> opened;

private:
	static wstring key(const wstring& path)
	{
		if (path.rfind(L"\\\\?\\", 0) == 0 || path.rfind(L"\\??\\", 0) == 0)
			return lowered(path.substr(4));
		return lowered(path);
	}

	std::map<wstring, ConfigPathPolicy::Entry> entries;
	std::map<wstring, wstring> finalPaths;
};

bool allows(const TableFileSystem& fileSystem, const wstring& path, const wstring& configPath, wstring& reason)
{
	reason.clear();
	return ConfigPathPolicy::allowsOpen(path, configPath, reason, fileSystem);
}

const wstring kLocalConfig = L"C:\\cfg\\config.txt";

void testLinkToShareIsRefusedBeforeReachingIt()
{
	TableFileSystem fileSystem;
	fileSystem.link(L"C:\\irs\\nas", L"\\??\\UNC\\nas\\eq");
	wstring reason;
	harness.expectFalse(allows(fileSystem, L"C:\\irs\\nas\\room.wav", kLocalConfig, reason),
		"a link to a share is refused like the share");
	harness.expectTrue(contains(reason, L"C:\\irs\\nas") && contains(reason, L"network share"),
		"the refusal names the link and the share");
	harness.expectFalse(fileSystem.readAnything(L"\\\\"), "nothing on the share is read to judge it");
	harness.expectTrue(fileSystem.opened.empty(), "and nothing is opened through the link");
}

void testWalkContinuesThroughLocalLinks()
{
	TableFileSystem fileSystem;
	fileSystem.link(L"C:\\irs\\j", L"\\??\\D:\\real");
	wstring reason;
	harness.expectTrue(allows(fileSystem, L"C:\\irs\\j\\room.wav", kLocalConfig, reason),
		"a junction to a local folder is allowed");
	harness.expectTrue(fileSystem.readAnything(L"D:\\real\\room.wav"), "and the walk goes on inside its target");

	fileSystem.link(L"D:\\real\\deep", L"\\??\\UNC\\nas\\eq");
	harness.expectFalse(allows(fileSystem, L"C:\\irs\\j\\deep\\room.wav", kLocalConfig, reason),
		"a second link inside the first one's target is judged too");
	harness.expectTrue(contains(reason, L"D:\\real\\deep") && !contains(reason, L"\\\\?\\D:"),
		"the refusal names the second link as a plain drive path");
}

void testRelativeLinks()
{
	TableFileSystem fileSystem;
	fileSystem.link(L"C:\\a\\b\\rel", L"..\\sibling", true);
	wstring reason;
	harness.expectTrue(allows(fileSystem, L"C:\\a\\b\\rel\\x.wav", kLocalConfig, reason),
		"a relative link to a local folder is allowed");
	harness.expectTrue(fileSystem.readAnything(L"C:\\a\\sibling\\x.wav"),
		"its target is taken from the link's own folder, with .. folded");

	fileSystem.link(L"C:\\a\\b\\up", L"..\\..\\far", true);
	fileSystem.link(L"C:\\far", L"\\??\\UNC\\nas\\eq");
	harness.expectFalse(allows(fileSystem, L"C:\\a\\b\\up\\x.wav", kLocalConfig, reason),
		"a relative link that ends at a link to a share is refused");
	harness.expectTrue(contains(reason, L"C:\\far"), "the refusal names the link that left the drive");
}

void testFoldingComesBeforeTheWalk()
{
	TableFileSystem fileSystem;
	fileSystem.link(L"C:\\link", L"\\??\\UNC\\nas\\eq");
	wstring reason;
	harness.expectTrue(allows(fileSystem, L"C:\\link\\..\\x.wav", kLocalConfig, reason),
		"Win32 folds .. before the open, so C:\\link\\..\\x.wav is C:\\x.wav");
	harness.expectFalse(fileSystem.readAnything(L"C:\\link"), "and the link it folds away is never read");
}

void testMissingComponentEndsTheWalk()
{
	TableFileSystem fileSystem;
	fileSystem.mark(L"C:\\gone", Kind::Missing);
	fileSystem.link(L"C:\\gone\\x", L"\\??\\UNC\\nas\\eq");
	wstring reason;
	harness.expectTrue(allows(fileSystem, L"C:\\gone\\x\\ir.wav", kLocalConfig, reason),
		"a path that does not exist is left to the open to report");
	harness.expectTrue(fileSystem.read.size() == 1, "nothing past the missing component is read");
}

void testUnexaminableComponentFallsBackToTheFinalPath()
{
	TableFileSystem fileSystem;
	fileSystem.mark(L"C:\\Users\\me", Kind::Unexaminable);
	wstring reason;

	fileSystem.arrives(L"C:\\Users\\me\\ir.wav", L"\\\\?\\UNC\\nas\\eq\\ir.wav");
	harness.expectFalse(allows(fileSystem, L"C:\\Users\\me\\ir.wav", kLocalConfig, reason),
		"past a folder the walk cannot read, an open that arrives on a share is refused");
	harness.expectTrue(contains(reason, L"C:\\Users\\me"), "the refusal names the folder it could not read");
	harness.expectTrue(fileSystem.opened.size() == 1, "the final path is asked once");

	fileSystem.arrives(L"C:\\Users\\me\\ir.wav", L"\\\\?\\C:\\Users\\me\\ir.wav");
	harness.expectTrue(allows(fileSystem, L"C:\\Users\\me\\ir.wav", kLocalConfig, reason),
		"an open that arrives on a local drive is allowed");

	fileSystem.arrives(L"C:\\Users\\me\\ir.wav", L"");
	harness.expectTrue(allows(fileSystem, L"C:\\Users\\me\\ir.wav", kLocalConfig, reason),
		"a file that cannot be opened is left to the open to report");
}

void testOtherLinksAndLoops()
{
	TableFileSystem fileSystem;
	fileSystem.mark(L"C:\\wsl", Kind::OtherLink);
	wstring reason;
	harness.expectFalse(allows(fileSystem, L"C:\\wsl\\ir.wav", kLocalConfig, reason),
		"a redirecting reparse point of another kind is refused");
	harness.expectTrue(contains(reason, L"does not follow"), "the refusal says why");

	fileSystem.link(L"C:\\loop", L"\\??\\C:\\loop");
	harness.expectFalse(allows(fileSystem, L"C:\\loop\\ir.wav", kLocalConfig, reason), "a link loop is refused");
	harness.expectTrue(contains(reason, L"more than 63 links"), "as too many links");
}

void testNetworkDrive()
{
	TableFileSystem fileSystem;
	fileSystem.networkDrives.insert(L'Z');
	wstring reason;
	harness.expectFalse(allows(fileSystem, L"Z:\\ir.wav", kLocalConfig, reason),
		"a file on a network drive is refused like the share");
	harness.expectTrue(contains(reason, L"network share"), "with the share reason");
	harness.expectTrue(allows(fileSystem, L"Z:\\irs\\ir.wav", L"Z:\\cfg\\config.txt", reason),
		"a config on that drive may name it");
	harness.expectFalse(fileSystem.readAnything(L"Z:"), "a network drive is not walked");
}

void testConfigReachedThroughALink()
{
	TableFileSystem fileSystem;
	fileSystem.link(L"C:\\cfglink", L"\\??\\UNC\\nas\\eq");
	const wstring configPath = L"C:\\cfglink\\config.txt";
	wstring reason;
	harness.expectTrue(allows(fileSystem, L"\\\\nas\\eq\\ir.wav", configPath, reason),
		"a config reached through a link to a share may name that share");
	harness.expectTrue(allows(fileSystem, L"C:\\cfglink\\ir.wav", configPath, reason),
		"and may name its own folder through the same link");
	harness.expectFalse(allows(fileSystem, L"\\\\nas\\other\\ir.wav", configPath, reason),
		"but not another share");
}

void testSharePathsAreNotWalked()
{
	TableFileSystem fileSystem;
	wstring reason;
	harness.expectTrue(allows(fileSystem, L"\\\\nas\\eq\\sub\\ir.wav", L"\\\\nas\\eq\\config.txt", reason),
		"a same-share reference is allowed");
	harness.expectTrue(fileSystem.read.empty() && fileSystem.opened.empty(), "without reading anything");
}

// A junction, which needs no privilege: a directory whose mount-point
// reparse data names substitute.
bool makeJunction(const wstring& linkPath, const wstring& substitute)
{
	if (!CreateDirectoryW(linkPath.c_str(), nullptr))
		return false;
	const winutil::UniqueHandle directory(CreateFileW(linkPath.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
	if (!directory)
		return false;

	const wstring print = substitute.rfind(L"\\??\\", 0) == 0 ? substitute.substr(4) : substitute;
	const size_t substituteBytes = substitute.size() * sizeof(wchar_t);
	const size_t printBytes = print.size() * sizeof(wchar_t);
	std::vector<unsigned char> buffer(16 + substituteBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t));
	const ULONG tag = IO_REPARSE_TAG_MOUNT_POINT;
	const USHORT dataLength = static_cast<USHORT>(buffer.size() - 8);
	const USHORT names[4] = {0, static_cast<USHORT>(substituteBytes),
		static_cast<USHORT>(substituteBytes + sizeof(wchar_t)), static_cast<USHORT>(printBytes)};
	std::memcpy(buffer.data(), &tag, sizeof(tag));
	std::memcpy(buffer.data() + 4, &dataLength, sizeof(dataLength));
	std::memcpy(buffer.data() + 8, names, sizeof(names));
	std::memcpy(buffer.data() + 16, substitute.data(), substituteBytes);
	std::memcpy(buffer.data() + 16 + substituteBytes + sizeof(wchar_t), print.data(), printBytes);

	DWORD returned = 0;
	return DeviceIoControl(directory.get(), FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()),
		nullptr, 0, &returned, nullptr) != FALSE;
}

void testRealJunctions()
{
	wchar_t temp[MAX_PATH + 1] = {};
	GetTempPathW(MAX_PATH, temp);
	const wstring root = wstring(temp) + L"eapo-xt-configpath-" + std::to_wstring(GetCurrentProcessId());
	CreateDirectoryW(root.c_str(), nullptr);
	CreateDirectoryW((root + L"\\real").c_str(), nullptr);
	const wstring file = root + L"\\real\\ir.wav";
	CloseHandle(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	const wstring configPath = root + L"\\config.txt";

	expectAllowed(file, configPath, "a plain local file on disk is allowed");

	if (makeJunction(root + L"\\local", L"\\??\\" + root + L"\\real"))
		expectAllowed(root + L"\\local\\ir.wav", configPath, "a real junction to a local folder is allowed");
	else
		std::printf("ConfigPathPolicyTests: could not create a junction (error %lu); real local-junction case not run\n",
			GetLastError());

	if (makeJunction(root + L"\\share", L"\\??\\UNC\\eapo-xt-no-such-host.invalid\\share"))
	{
		wstring reason;
		harness.expectFalse(ConfigPathPolicy::allowsOpen(root + L"\\share\\ir.wav", configPath, reason),
			"a real junction whose target is a share is refused");
		harness.expectTrue(contains(reason, root + L"\\share"), "naming the junction");
	}
	else
		std::printf("ConfigPathPolicyTests: the file system refused a junction to a share (error %lu); that case not run\n",
			GetLastError());

	RemoveDirectoryW((root + L"\\share").c_str());
	RemoveDirectoryW((root + L"\\local").c_str());
	DeleteFileW(file.c_str());
	RemoveDirectoryW((root + L"\\real").c_str());
	RemoveDirectoryW(root.c_str());
}
}

void runConfigPathPolicyTests()
{
	testRemoteRoot();
	testLocalConfigPolicy();
	testSameShareExemption();
	testEmptyConfigPath();
	testLinkToShareIsRefusedBeforeReachingIt();
	testWalkContinuesThroughLocalLinks();
	testRelativeLinks();
	testFoldingComesBeforeTheWalk();
	testMissingComponentEndsTheWalk();
	testUnexaminableComponentFallsBackToTheFinalPath();
	testOtherLinksAndLoops();
	testNetworkDrive();
	testConfigReachedThroughALink();
	testSharePathsAreNotWalked();
	testRealJunctions();

	harness.report();
}
