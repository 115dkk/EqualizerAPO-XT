/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	ConfigFileReference's judged paths on a real file system: what a held
	JudgedPath pins (parents, the leaf), how it reads through an ancestor the
	user may not list or traverse, and the refusals it hands back instead of a
	path (sharing violation, network share).
*/

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include "engine/ConfigurationFileReader.h"
#include "filters/ConfigFileReference.h"
#include "platform/windows/Win32Resource.h"

#include "EngineOrchestrationTestSupport.h"

static_assert(!std::is_constructible_v<JudgedPath, std::wstring>);
static_assert(!std::is_constructible_v<JudgedPath, const wchar_t*>);
static_assert(!std::is_copy_constructible_v<JudgedPath>);

namespace
{
// Restore through the original security handle even after a rename or a
// failed assertion. Owner rights grant READ_CONTROL/WRITE_DAC, not list access.
class DirectoryDaclScope
{
public:
	explicit DirectoryDaclScope(const std::wstring& path)
		: handle(CreateFileW(path.c_str(), READ_CONTROL | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr))
	{
		if (!handle || GetSecurityInfo(handle.get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
			nullptr, nullptr, &originalAcl, nullptr, &original) != ERROR_SUCCESS)
			throw std::runtime_error("could not save directory DACL");
		DWORD revision = 0;
		GetSecurityDescriptorControl(original, &control, &revision);
	}
	~DirectoryDaclScope()
	{
		if (original != nullptr)
		{
			const DWORD result = SetSecurityInfo(handle.get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION
				| ((control & SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION),
				nullptr, nullptr, originalAcl, nullptr);
			LocalFree(original);
			if (result != ERROR_SUCCESS)
				std::abort();
		}
	}
	// 0x21 is FILE_LIST_DIRECTORY | FILE_TRAVERSE on a directory and
	// FILE_READ_DATA | FILE_EXECUTE on a file.
	void denyTraversal(const std::wstring& sid)
	{
		// SetSecurityInfo re-propagates inheritance to existing children, so the
		// grant is inheritable (the children keep read access) while the deny
		// stays on this object alone.
		const std::wstring sddl = L"D:P(D;;0x21;;;" + sid + L")(A;OICI;FA;;;" + sid + L")";
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
			throw std::runtime_error("could not build traversal-denying DACL");
		PACL acl = nullptr;
		BOOL present = FALSE, defaulted = FALSE;
		const BOOL decoded = GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted);
		const DWORD result = decoded && present ? SetSecurityInfo(handle.get(), SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr) : ERROR_INVALID_ACL;
		LocalFree(descriptor);
		if (result != ERROR_SUCCESS)
			throw std::runtime_error("could not install traversal-denying DACL");
	}
private:
	winutil::UniqueHandle handle;
	PSECURITY_DESCRIPTOR original = nullptr;
	PACL originalAcl = nullptr;
	SECURITY_DESCRIPTOR_CONTROL control = 0;
};

// SetFileInformationByHandle(FileRenameInfoEx) with POSIX semantics, which a
// hostile local user could use instead of MoveFileW. The buffer is laid out
// here because FILE_RENAME_INFO::Flags needs _WIN32_WINNT_WIN10_RS1 headers.
bool posixRename(const std::wstring& directory, const std::wstring& newName, DWORD& error)
{
	const winutil::UniqueHandle handle(CreateFileW(directory.c_str(), DELETE | SYNCHRONIZE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
	if (!handle)
	{
		error = GetLastError();
		return false;
	}
	struct RenameInfo
	{
		DWORD flags;
		HANDLE rootDirectory;
		DWORD fileNameLength;
		WCHAR fileName[1];
	};
	std::vector<unsigned char> buffer(offsetof(RenameInfo, fileName) + (newName.size() + 1) * sizeof(wchar_t));
	auto* info = reinterpret_cast<RenameInfo*>(buffer.data());
	info->flags = 0x2; // FILE_RENAME_FLAG_POSIX_SEMANTICS
	info->fileNameLength = static_cast<DWORD>(newName.size() * sizeof(wchar_t));
	std::copy(newName.begin(), newName.end(), info->fileName);
	// SetFileInformationByHandle expands the name as a DOS path, so a bare
	// name would land in the current directory; callers pass a full path.
	const bool renamed = SetFileInformationByHandle(handle.get(), FileRenameInfoEx, info,
		static_cast<DWORD>(buffer.size())) != FALSE;
	error = renamed ? ERROR_SUCCESS : GetLastError();
	return renamed;
}
}

void testJudgedPathAttributesOnlyAncestor(test::Harness& harness)
{
	const std::wstring parent = testDirectory() + L"\\acl-parent";
	const std::wstring middle = parent + L"\\middle";
	const std::wstring moved = parent + L"\\moved";
	const std::wstring config = middle + L"\\config";
	const std::wstring file = config + L"\\file.txt";
	for (const auto& directory : {parent, middle, config})
		harness.require(CreateDirectoryW(directory.c_str(), nullptr) != FALSE, "create ACL traversal fixture");
	{
		std::ofstream stream(file);
		stream << "granted child";
	}
	winutil::UniqueHandle token;
	harness.require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()) != FALSE, "query current test user");
	DWORD size = 0;
	GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
	std::vector<unsigned char> user(size);
	harness.require(GetTokenInformation(token.get(), TokenUser, user.data(), size, &size) != FALSE, "read test user SID");
	wchar_t* sidText = nullptr;
	harness.require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sidText) != FALSE,
		"format test user SID");
	const std::wstring sid(sidText);
	LocalFree(sidText);
	{
		const auto baseline = ConfigFileReference::target(L"", file);
		harness.require(baseline.path.leaf() != nullptr, "ACL fixture initially readable");
		harness.expectEqual(baseline.path.attributesOnlyPinCount(), size_t(0), "fixture initially needs no attributes-only pin");
	}
	{
		DirectoryDaclScope dacl(middle);
		dacl.denyTraversal(sid);
		// The deny ACE is explicit and non-inheritable; config and file.txt
		// inherit only the grant.
		const winutil::UniqueHandle list(CreateFileW(middle.c_str(), FILE_LIST_DIRECTORY | SYNCHRONIZE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
		const DWORD listError = GetLastError();
		harness.expectTrue(!list && listError == ERROR_ACCESS_DENIED, "explicit deny defeats even the owner's list access");
		{
			const auto judged = ConfigFileReference::target(L"", file);
			harness.expectTrue(judged.refusal.empty() && judged.path.leaf() != nullptr, "attributes-only parent permits relative child open");
			harness.expectEqual(judged.path.attributesOnlyPinCount(), size_t(1), "only the denied middle component uses fallback access");
			harness.expectEqual(ConfigurationFileReader::read(judged.path).str(), std::string("granted child"),
				"read granted leaf through a parent without list or traverse rights");
			// middle's own pin is attributes-only and takes no part in share
			// checks, but the chain holds config and file.txt beneath it, and
			// NTFS refuses to rename a directory with open handles beneath it
			// (ERROR_ACCESS_DENIED; a share-mode block would be
			// ERROR_SHARING_VIOLATION). Both rename forms are tried.
			const BOOL renamed = MoveFileW(middle.c_str(), moved.c_str());
			const DWORD renameError = GetLastError();
			if (renamed)
				MoveFileW(moved.c_str(), middle.c_str());
			DWORD posixError = ERROR_SUCCESS;
			const bool posixRenamed = posixRename(middle, moved, posixError);
			if (posixRenamed)
				MoveFileW(moved.c_str(), middle.c_str());
			std::printf("A1b held attributes-only ancestor: MoveFileW error %lu, POSIX rename error %lu\n",
				renamed ? ERROR_SUCCESS : renameError, posixError);
			harness.expectTrue(!renamed && renameError == ERROR_ACCESS_DENIED,
				"an ancestor with held descendants cannot be renamed");
			harness.expectTrue(!posixRenamed && posixError == ERROR_ACCESS_DENIED,
				"nor through a POSIX-semantics rename");
			harness.expectEqual(ConfigurationFileReader::read(judged.path).str(), std::string("granted child"),
				"the held leaf still reads the granted bytes");
		}
		// Released, the same folder renames both ways, so the refusals above
		// came from the held chain.
		DWORD posixError = ERROR_SUCCESS;
		const bool posixRenamed = posixRename(middle, moved, posixError);
		harness.expectTrue(posixRenamed, "POSIX rename succeeds once the judged path is released");
		if (posixRenamed)
			harness.expectTrue(MoveFileW(moved.c_str(), middle.c_str()) != FALSE, "rename back once the judged path is released");
		// Only this thread's copied token changes; the process token and its
		// worker threads keep their privileges. Revert before restoring the ACL.
		if (!ImpersonateSelf(SecurityImpersonation))
			throw std::runtime_error("could not create traversal test thread token");
		struct RevertScope { ~RevertScope() { if (!RevertToSelf()) std::abort(); } } revert;
		winutil::UniqueHandle threadToken;
		if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, TRUE, threadToken.put()))
			throw std::runtime_error("could not open traversal test thread token");
		TOKEN_PRIVILEGES change = {};
		change.PrivilegeCount = 1;
		if (!LookupPrivilegeValueW(nullptr, SE_CHANGE_NOTIFY_NAME, &change.Privileges[0].Luid))
			throw std::runtime_error("could not find bypass-traverse privilege");
		TOKEN_PRIVILEGES previous = {};
		DWORD returned = 0;
		if (!AdjustTokenPrivileges(threadToken.get(), FALSE, &change, sizeof(previous), &previous, &returned)
			|| GetLastError() != ERROR_SUCCESS)
			throw std::runtime_error("could not disable bypass-traverse privilege");
		harness.expectTrue(previous.PrivilegeCount == 1 && (previous.Privileges[0].Attributes & SE_PRIVILEGE_ENABLED) != 0,
			"normal token initially has bypass-traverse enabled");
		const auto denied = ConfigFileReference::target(L"", file);
		std::printf("A1b attributes-only ancestor: enabled privilege reads child; disabled privilege error %lu, leaf %s\n",
			denied.error, denied.path.leaf() != nullptr ? "available" : "unavailable");
		harness.expectTrue(denied.path.empty() && denied.error == ERROR_ACCESS_DENIED,
			"relative traversal needs bypass privilege when the parent denies traversal");
	}
	{
		// Only ancestors may fall back: a file the engine cannot read is
		// refused, never handed to a reader as an attributes-only handle.
		DirectoryDaclScope dacl(file);
		dacl.denyTraversal(sid);
		const auto unreadable = ConfigFileReference::target(L"", file);
		harness.expectTrue(unreadable.path.empty() && unreadable.error == ERROR_ACCESS_DENIED && !unreadable.refusal.empty(),
			"an unreadable leaf is refused rather than held attributes-only");
	}
	harness.expectTrue(DeleteFileW(file.c_str()) != FALSE, "restored DACL permits fixture cleanup");
	harness.expectTrue(RemoveDirectoryW(config.c_str()) != FALSE, "remove granted config directory");
	harness.expectTrue(RemoveDirectoryW(middle.c_str()) != FALSE, "remove restored middle directory");
	harness.expectTrue(RemoveDirectoryW(parent.c_str()) != FALSE, "remove ACL fixture parent");
}

void testJudgedPathLifetime(test::Harness& harness)
{
	const std::wstring folder = testDirectory() + L"\\judged";
	const std::wstring renamed = folder + L"-renamed";
	CreateDirectoryW(folder.c_str(), nullptr);
	const std::wstring file = folder + L"\\leaf.txt";
	{
		std::ofstream stream(file);
		stream << "original";
	}
	{
		const auto judged = ConfigFileReference::target(L"", file);
		harness.require(judged.refusal.empty() && judged.path.leaf() != nullptr, "judged path owns its leaf");
		harness.expectFalse(MoveFileW(folder.c_str(), renamed.c_str()) != FALSE, "judged path pins its parent");
		harness.expectFalse(DeleteFileW(file.c_str()) != FALSE, "judged path pins its final file");
		harness.expectFalse(MoveFileW(file.c_str(), (file + L"-old").c_str()) != FALSE, "final file cannot be replaced by rename");
		// Developer Mode is optional; test its availability separately rather
		// than mistaking an existing-name error for a privilege check.
		const std::wstring link = folder + L"\\probe-link";
		if (CreateSymbolicLinkW(link.c_str(), file.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		{
			DeleteFileW(link.c_str());
			harness.expectFalse(CreateSymbolicLinkW(file.c_str(), L"\\\\eapo-no-host.invalid\\share\\file",
				SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE, "held final file cannot be replaced by a symlink");
		}
		else
			std::printf("JudgedPath: symlink replacement half skipped (creation unavailable, error %lu)\n", GetLastError());
		auto stream = ConfigurationFileReader::read(judged.path);
		harness.expectEqual(stream.str(), std::string("original"), "held file still supplies original bytes");
	}
	harness.expectTrue(MoveFileW(folder.c_str(), renamed.c_str()) != FALSE, "destroying judged path releases directory pins");
	MoveFileW(renamed.c_str(), folder.c_str());
	{
		winutil::UniqueHandle deleting(CreateFileW(file.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		harness.require(static_cast<bool>(deleting), "hold delete access to exercise sharing refusal");
		const auto refused = ConfigFileReference::target(L"", file);
		harness.expectTrue(refused.path.empty() && refused.error == ERROR_SHARING_VIOLATION
			&& refused.refusal.find(L"in use by another program") != std::wstring::npos,
			"sharing refusal has no judged path and clear retry advice");
		const auto retried = ConfigurationFileReader::judgeWithRetry(L"", file, nullptr, 5);
		harness.expectEqual(retried.error, DWORD(ERROR_SHARING_VIOLATION), "Include judgment retry has a deadline");
	}
	const auto remote = ConfigFileReference::target(file, L"\\\\eapo-no-host.invalid\\share\\file");
	harness.expectTrue(remote.path.empty() && !remote.refusal.empty(), "remote refusal has an empty judged path");
	DeleteFileW(file.c_str());
	RemoveDirectoryW(folder.c_str());
}
