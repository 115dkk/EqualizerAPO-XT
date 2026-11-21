/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include "platform/windows/Win32Resource.h"
#include "services/update/UpdateLeftovers.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
std::wstring joinPath(const std::wstring& parent, const std::wstring& child)
{
	return parent + L"\\" + child;
}

void clearReadOnly(const std::wstring& path, DWORD attributes)
{
	if ((attributes & FILE_ATTRIBUTE_READONLY) != 0)
		SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
}

void removeTree(const std::wstring& path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return;
	if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0
		|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		clearReadOnly(path, attributes);
		if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			RemoveDirectoryW(path.c_str());
		else
			DeleteFileW(path.c_str());
		return;
	}

	WIN32_FIND_DATAW data{};
	winutil::UniqueFindHandle findHandle(FindFirstFileW(joinPath(path, L"*").c_str(), &data));
	if (findHandle)
	{
		do
		{
			const std::wstring name(data.cFileName);
			if (name != L"." && name != L"..")
				removeTree(joinPath(path, name));
		}
		while (FindNextFileW(findHandle.get(), &data) != FALSE);
		findHandle.reset();
	}
	const DWORD currentAttributes = GetFileAttributesW(path.c_str());
	if (currentAttributes != INVALID_FILE_ATTRIBUTES)
		clearReadOnly(path, currentAttributes);
	RemoveDirectoryW(path.c_str());
}

class TemporaryTree
{
public:
	TemporaryTree()
	{
		wchar_t tempDirectory[MAX_PATH] = {};
		wchar_t temporaryFile[MAX_PATH] = {};
		const DWORD length = GetTempPathW(MAX_PATH, tempDirectory);
		if (length == 0 || length >= MAX_PATH
			|| GetTempFileNameW(tempDirectory, L"EUL", 0, temporaryFile) == 0)
		{
			return;
		}
		if (!DeleteFileW(temporaryFile) || !CreateDirectoryW(temporaryFile, nullptr))
			return;
		root_ = temporaryFile;
	}

	~TemporaryTree()
	{
		if (!root_.empty())
			removeTree(root_);
	}

	TemporaryTree(const TemporaryTree&) = delete;
	TemporaryTree& operator=(const TemporaryTree&) = delete;

	bool valid() const
	{
		return !root_.empty();
	}

	std::wstring path(const std::wstring& relative) const
	{
		return joinPath(root_, relative);
	}

	std::wstring directory(const std::wstring& relative) const
	{
		const std::wstring result = path(relative);
		CreateDirectoryW(result.c_str(), nullptr);
		return result;
	}

private:
	std::wstring root_;
};

bool createFile(const std::wstring& path)
{
	winutil::UniqueHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (!file)
		return false;
	const char contents[] = "fixture";
	DWORD written = 0;
	return WriteFile(file.get(), contents, static_cast<DWORD>(sizeof(contents) - 1), &written, nullptr) != FALSE
		&& written == sizeof(contents) - 1;
}

bool exists(const std::wstring& path)
{
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool containsPath(const std::vector<std::wstring>& paths, const std::wstring& expected)
{
	return std::find(paths.begin(), paths.end(), expected) != paths.end();
}

constexpr std::chrono::seconds kNoMinimumAge(0);
constexpr wchar_t kRootName[] = L"EqualizerAPO-XT-x64-avx2";
}

TEST_CASE(EditorLogicTests, testUpdateLeftoverNamePredicates)
{
	expectTrue(UpdateLeftovers::isTempEntryName(L"tmp_7qsZzf2BmCZAAZEY"),
		QStringLiteral("the observed VelopackTemp name matches"));
	expectFalse(UpdateLeftovers::isTempEntryName(L"tmp_123456789012345"),
		QStringLiteral("a 15-character temp suffix is rejected"));
	expectFalse(UpdateLeftovers::isTempEntryName(L"tmp_12345678901234567"),
		QStringLiteral("a 17-character temp suffix is rejected"));
	expectFalse(UpdateLeftovers::isTempEntryName(L"tmp_12345678901234_6"),
		QStringLiteral("a non-alphanumeric temp suffix is rejected"));

	expectTrue(UpdateLeftovers::isRollbackRootName(kRootName,
		L"EqualizerAPO-XT-x64-avx2_8qjHnVFof8m0f46T"),
		QStringLiteral("the observed underscore rollback name matches"));
	expectTrue(UpdateLeftovers::isRollbackRootName(kRootName,
		L"EqualizerAPO-XT-x64-avx2.NYP6wLbMU4JBKY4P"),
		QStringLiteral("the observed dot rollback name matches"));
	expectTrue(UpdateLeftovers::isRollbackRootName(L"equalizerapo-xt-X64-AVX2",
		L"EQUALIZERAPO-XT-x64-avx2_1234567890123456"),
		QStringLiteral("the install root comparison is case-insensitive"));
	expectFalse(UpdateLeftovers::isRollbackRootName(kRootName,
		L"EqualizerAPO-XT-x64-avx2_123456789012345"),
		QStringLiteral("a 15-character rollback suffix is rejected"));
	expectFalse(UpdateLeftovers::isRollbackRootName(kRootName,
		L"EqualizerAPO-XT-x64-avx2_12345678901234567"),
		QStringLiteral("a 17-character rollback suffix is rejected"));
	expectFalse(UpdateLeftovers::isRollbackRootName(kRootName,
		L"EqualizerAPO-XT-x64-avx2_12345678901234-6"),
		QStringLiteral("a non-alphanumeric rollback suffix is rejected"));
	expectFalse(UpdateLeftovers::isRollbackRootName(kRootName,
		L"EqualizerAPO-XT-x64-avx2-1234567890123456"),
		QStringLiteral("an unsupported rollback separator is rejected"));
	expectFalse(UpdateLeftovers::isRollbackRootName(L"EqualizerAPO-XT",
		L"EqualizerAPO-XT-x64-avx2_1234567890123456"),
		QStringLiteral("a different install root name is rejected"));
	expectFalse(UpdateLeftovers::isRollbackRootName(std::wstring_view(), L"_1234567890123456"),
		QStringLiteral("an empty install root name never matches"));
}

TEST_CASE(EditorLogicTests, testUpdateLeftoversRequirePackagedInstallRoot)
{
	TemporaryTree tree;
	requireTrue(tree.valid(), QStringLiteral("create a unique update-leftover fixture"));
	const std::wstring root = tree.directory(kRootName);
	tree.directory(std::wstring(kRootName) + L"\\packages");
	tree.directory(std::wstring(kRootName) + L"\\packages\\VelopackTemp");
	tree.directory(std::wstring(kRootName) + L"\\packages\\VelopackTemp\\tmp_1234567890123456");

	const std::vector<std::wstring> found = UpdateLeftovers::find(root, kNoMinimumAge);
	expectEqual(static_cast<int>(found.size()), 0,
		QStringLiteral("an install root without Update.exe is not searched"));
}

TEST_CASE(EditorLogicTests, testUpdateLeftoversFindBothKindsAndAgeGate)
{
	TemporaryTree tree;
	requireTrue(tree.valid(), QStringLiteral("create a unique update-leftover fixture"));
	const std::wstring root = tree.directory(kRootName);
	requireTrue(createFile(joinPath(root, L"Update.exe")), QStringLiteral("create the install marker"));
	tree.directory(std::wstring(kRootName) + L"\\packages");
	tree.directory(std::wstring(kRootName) + L"\\packages\\VelopackTemp");
	const std::wstring tempLeftover = tree.directory(
		std::wstring(kRootName) + L"\\packages\\VelopackTemp\\tmp_7qsZzf2BmCZAAZEY");

	const std::wstring rollbackName = std::wstring(kRootName) + L"_8qjHnVFof8m0f46T";
	const std::wstring rollback = tree.directory(rollbackName);
	tree.directory(rollbackName + L"\\current");
	const std::wstring noShape = tree.directory(std::wstring(kRootName) + L"_1111111111111111");
	const std::wstring nonMatching = tree.directory(L"EqualizerAPO-XT");
	const std::wstring matchingFile = tree.path(std::wstring(kRootName) + L"_2222222222222222");
	requireTrue(createFile(matchingFile), QStringLiteral("create a file with a rollback-like name"));

	const std::vector<std::wstring> fresh = UpdateLeftovers::find(root);
	expectEqual(static_cast<int>(fresh.size()), 0,
		QStringLiteral("fresh candidates do not pass the default one-hour age"));

	const std::vector<std::wstring> found = UpdateLeftovers::find(root + L"\\\\", kNoMinimumAge);
	expectEqual(static_cast<int>(found.size()), 2,
		QStringLiteral("zero minimum age finds exactly the two shaped directories"));
	expectTrue(containsPath(found, tempLeftover), QStringLiteral("the VelopackTemp leftover is found"));
	expectTrue(containsPath(found, rollback), QStringLiteral("the rollback root is found"));
	expectFalse(containsPath(found, noShape), QStringLiteral("a rollback-like directory without product shape is ignored"));
	expectFalse(containsPath(found, nonMatching), QStringLiteral("a shorter unrelated product name is ignored"));
	expectFalse(containsPath(found, matchingFile), QStringLiteral("a rollback-like file is ignored"));
}

TEST_CASE(EditorLogicTests, testUpdateLeftoversSweepNestedReadOnlyContent)
{
	TemporaryTree tree;
	requireTrue(tree.valid(), QStringLiteral("create a unique update-leftover fixture"));
	const std::wstring root = tree.directory(kRootName);
	requireTrue(createFile(joinPath(root, L"Update.exe")), QStringLiteral("create the install marker"));
	const std::wstring rollbackName = std::wstring(kRootName) + L".NYP6wLbMU4JBKY4P";
	const std::wstring rollback = tree.directory(rollbackName);
	const std::wstring rollbackUpdate = joinPath(rollback, L"Update.exe");
	requireTrue(createFile(rollbackUpdate), QStringLiteral("create the rollback updater"));
	tree.directory(rollbackName + L"\\current");
	tree.directory(rollbackName + L"\\current\\nested");
	const std::wstring readOnly = joinPath(tree.path(rollbackName + L"\\current\\nested"), L"payload.bin");
	requireTrue(createFile(readOnly), QStringLiteral("create the nested rollback payload"));
	const DWORD payloadAttributes = GetFileAttributesW(readOnly.c_str());
	requireTrue(payloadAttributes != INVALID_FILE_ATTRIBUTES
		&& SetFileAttributesW(readOnly.c_str(), payloadAttributes | FILE_ATTRIBUTE_READONLY) != FALSE,
		QStringLiteral("make the nested payload read-only"));
	requireTrue(createFile(joinPath(rollback, L"velopack.log")), QStringLiteral("create another rollback entry"));
	const std::wstring unrelated = tree.directory(L"unrelated");
	const std::wstring unrelatedFile = joinPath(unrelated, L"keep.txt");
	requireTrue(createFile(unrelatedFile), QStringLiteral("create an unrelated sibling file"));

	const UpdateLeftovers::SweepResult result = UpdateLeftovers::sweep(root, kNoMinimumAge);
	expectEqual(static_cast<int>(result.found), 1, QStringLiteral("the rollback leftover is counted"));
	expectEqual(static_cast<int>(result.removed), 1, QStringLiteral("the rollback leftover is removed completely"));
	expectFalse(exists(rollback), QStringLiteral("the rollback top directory is gone"));
	expectTrue(exists(root), QStringLiteral("the active install root remains"));
	expectTrue(exists(joinPath(root, L"Update.exe")), QStringLiteral("the active install marker remains"));
	expectTrue(exists(unrelatedFile), QStringLiteral("an unrelated sibling remains intact"));
}

TEST_CASE(EditorLogicTests, testUpdateLeftoversSweepContinuesPastLockedFile)
{
	TemporaryTree tree;
	requireTrue(tree.valid(), QStringLiteral("create a unique update-leftover fixture"));
	const std::wstring root = tree.directory(kRootName);
	requireTrue(createFile(joinPath(root, L"Update.exe")), QStringLiteral("create the install marker"));
	tree.directory(std::wstring(kRootName) + L"\\packages");
	tree.directory(std::wstring(kRootName) + L"\\packages\\VelopackTemp");
	const std::wstring leftoverRelative = std::wstring(kRootName)
		+ L"\\packages\\VelopackTemp\\tmp_1234567890123456";
	const std::wstring leftover = tree.directory(leftoverRelative);
	tree.directory(leftoverRelative + L"\\nested");
	const std::wstring lockedPath = joinPath(leftover, L"locked.bin");
	const std::wstring otherPath = joinPath(leftover, L"other.bin");
	const std::wstring nestedPath = joinPath(tree.path(leftoverRelative + L"\\nested"), L"other.bin");
	requireTrue(createFile(lockedPath) && createFile(otherPath) && createFile(nestedPath),
		QStringLiteral("create the locked-leftover contents"));
	winutil::UniqueHandle locked(CreateFileW(lockedPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	requireTrue(static_cast<bool>(locked), QStringLiteral("open one file without delete sharing"));

	const UpdateLeftovers::SweepResult first = UpdateLeftovers::sweep(root, kNoMinimumAge);
	expectEqual(static_cast<int>(first.found), 1, QStringLiteral("the locked leftover is found"));
	expectEqual(static_cast<int>(first.removed), 0, QStringLiteral("the locked leftover is reported as remaining"));
	expectTrue(exists(lockedPath), QStringLiteral("the locked file remains"));
	expectFalse(exists(otherPath), QStringLiteral("an unlocked sibling is deleted despite the failure"));
	expectFalse(exists(nestedPath), QStringLiteral("unlocked nested content is deleted despite the failure"));

	locked.reset();
	const UpdateLeftovers::SweepResult second = UpdateLeftovers::sweep(root, kNoMinimumAge);
	expectEqual(static_cast<int>(second.found), 1, QStringLiteral("the partial leftover is found on the next sweep"));
	expectEqual(static_cast<int>(second.removed), 1, QStringLiteral("the next sweep removes the unlocked remainder"));
	expectFalse(exists(leftover), QStringLiteral("the leftover top directory is gone after retry"));
}

// A rollback copy is recognised by Update.exe or current. While a file outside
// them is still locked they must stay, or the remainder would never be found
// again (review of #510).
TEST_CASE(EditorLogicTests, testUpdateLeftoversRollbackKeepsMarkersWhileOthersRemain)
{
	TemporaryTree tree;
	requireTrue(tree.valid(), QStringLiteral("create a unique update-leftover fixture"));
	const std::wstring root = tree.directory(kRootName);
	requireTrue(createFile(joinPath(root, L"Update.exe")), QStringLiteral("create the install marker"));
	const std::wstring rollbackRelative = std::wstring(kRootName) + L"_1234567890abcdef";
	const std::wstring rollback = tree.directory(rollbackRelative);
	const std::wstring packages = tree.directory(rollbackRelative + L"\\packages");
	const std::wstring current = tree.directory(rollbackRelative + L"\\current");
	const std::wstring updateExe = joinPath(rollback, L"Update.exe");
	const std::wstring lockedPath = joinPath(packages, L"locked.nupkg");
	const std::wstring otherPath = joinPath(packages, L"other.nupkg");
	const std::wstring currentFile = joinPath(current, L"Editor.exe");
	requireTrue(createFile(updateExe) && createFile(lockedPath) && createFile(otherPath) && createFile(currentFile),
		QStringLiteral("create the rollback copy's contents"));
	winutil::UniqueHandle locked(CreateFileW(lockedPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	requireTrue(static_cast<bool>(locked), QStringLiteral("open a packages file without delete sharing"));

	const UpdateLeftovers::SweepResult first = UpdateLeftovers::sweep(root, kNoMinimumAge);
	expectEqual(static_cast<int>(first.found), 1, QStringLiteral("the rollback copy is found"));
	expectEqual(static_cast<int>(first.removed), 0, QStringLiteral("the rollback copy is reported as remaining"));
	expectTrue(exists(lockedPath), QStringLiteral("the locked packages file remains"));
	expectFalse(exists(otherPath), QStringLiteral("the unlocked packages file is deleted"));
	expectTrue(exists(updateExe) && exists(currentFile),
		QStringLiteral("the markers stay while other content remains"));

	locked.reset();
	const UpdateLeftovers::SweepResult second = UpdateLeftovers::sweep(root, kNoMinimumAge);
	expectEqual(static_cast<int>(second.found), 1, QStringLiteral("the partial rollback copy is found again"));
	expectEqual(static_cast<int>(second.removed), 1, QStringLiteral("the retry removes it"));
	expectFalse(exists(rollback), QStringLiteral("the rollback copy is gone after the retry"));
	expectTrue(exists(joinPath(root, L"Update.exe")), QStringLiteral("the install root is untouched"));
}

TEST_CASE(EditorLogicTests, testUpdateLeftoversSweepDoesNotFollowJunction)
{
	TemporaryTree tree;
	requireTrue(tree.valid(), QStringLiteral("create a unique update-leftover fixture"));
	const std::wstring root = tree.directory(kRootName);
	requireTrue(createFile(joinPath(root, L"Update.exe")), QStringLiteral("create the install marker"));
	tree.directory(std::wstring(kRootName) + L"\\packages");
	tree.directory(std::wstring(kRootName) + L"\\packages\\VelopackTemp");
	const std::wstring leftover = tree.directory(
		std::wstring(kRootName) + L"\\packages\\VelopackTemp\\tmp_ABCDEFGHIJKLMNOP");
	const std::wstring outside = tree.directory(L"outside-target");
	const std::wstring outsideFile = joinPath(outside, L"keep.txt");
	requireTrue(createFile(outsideFile), QStringLiteral("create the junction target file"));
	const std::wstring link = joinPath(leftover, L"outside-link");
	const std::wstring command = L"cmd.exe /d /c mklink /J \"" + link + L"\" \"" + outside
		+ L"\" >nul 2>&1";
	const int junctionResult = _wsystem(command.c_str());
	const DWORD linkAttributes = GetFileAttributesW(link.c_str());
	if (junctionResult != 0 || linkAttributes == INVALID_FILE_ATTRIBUTES
		|| (linkAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
	{
		harness.skip("junction creation failed; the reparse-target preservation check was skipped");
		return;
	}

	const UpdateLeftovers::SweepResult result = UpdateLeftovers::sweep(root, kNoMinimumAge);
	expectEqual(static_cast<int>(result.found), 1, QStringLiteral("the leftover containing a junction is found"));
	expectEqual(static_cast<int>(result.removed), 1, QStringLiteral("the junction itself and leftover are removed"));
	expectFalse(exists(leftover), QStringLiteral("the leftover top directory is gone"));
	expectTrue(exists(outsideFile), QStringLiteral("the file behind the junction remains untouched"));
}
