/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.
    Copyright (C) 2026 115dkk
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <optional>
#include <string>

namespace EqAPO::Import::CallerProfileCheck
{
class FileSystem
{
public:
    virtual ~FileSystem() = default;
    virtual std::wstring profilesRoot() const = 0;
    virtual bool isFixedDrive(const std::wstring& root) const = 0;
    virtual bool isPlainDirectory(const std::wstring& path, std::wstring& reason) const = 0;
};

// Returns the normalized LOCALAPPDATA, or a rejection reason. No ownership
// test: domain accounts and migrated profiles have different valid owners.
std::optional<std::wstring> verify(const std::wstring& path, std::wstring& reason);
// Read-only snapshot, never authority for elevated file writes.
bool verifyPreparedRoot(const std::wstring& path, std::wstring& reason);
std::optional<std::wstring> verify(const std::wstring& path, std::wstring& reason,
    const FileSystem& fileSystem);
}
