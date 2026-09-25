/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.
    Copyright (C) 2026 115dkk
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "CallerProfileCheck.h"

#include <objbase.h>
#include <shlobj.h>

#include "services/security/ConfigDirectoryHandles.h"

namespace EqAPO::Import::CallerProfileCheck
{
namespace
{
class WindowsFileSystem : public FileSystem
{
public:
    std::wstring profilesRoot() const override
    {
        winutil::UniqueCoTaskMemPtr<wchar_t> path;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_UserProfiles, 0, nullptr, path.put())))
            return {};
        return path.get();
    }

    bool isFixedDrive(const std::wstring& root) const override
    {
        return GetDriveTypeW(root.c_str()) == DRIVE_FIXED;
    }

    bool isPlainDirectory(const std::wstring& path, std::wstring& reason) const override
    {
        // Retain ancestors against rename during this read-only snapshot.
        // This does not prevent in-place reparse conversion; no elevated
        // file mutation may rely on the snapshot.
        auto handle = configaccess::openDirectory(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, reason);
        if (!handle)
            return false;
        handles.push_back(std::move(handle));
        return true;
    }

private:
    mutable std::vector<winutil::UniqueHandle> handles;
};
}

bool verifyPreparedRoot(const std::wstring& path, std::wstring& reason)
{
    const std::wstring full = configaccess::fullLocalPath(path);
    if (full.empty())
    {
        reason = L"not an absolute local path";
        return false;
    }
    const WindowsFileSystem fileSystem;
    for (const auto& component : configaccess::directoryChain(full))
    {
        if (!fileSystem.isPlainDirectory(component, reason))
            return false;
    }
    return true;
}

std::optional<std::wstring> verify(const std::wstring& path, std::wstring& reason)
{
    const WindowsFileSystem fileSystem;
    return verify(path, reason, fileSystem);
}

std::optional<std::wstring> verify(const std::wstring& path, std::wstring& reason,
    const FileSystem& fileSystem)
{
    reason.clear();
    const std::wstring full = configaccess::fullLocalPath(path);
    if (full.empty())
        reason = L"not an absolute local drive path";
    else if (!fileSystem.isFixedDrive(full.substr(0, 3)))
        reason = L"not on a fixed local drive";
    else
    {
        const std::wstring root = configaccess::fullLocalPath(fileSystem.profilesRoot());
        const auto chain = configaccess::directoryChain(full);
        const auto rootChain = root.empty() ? std::vector<std::wstring>() : configaccess::directoryChain(root);
        if (root.empty() || chain.size() != rootChain.size() + 3
            || !configaccess::samePath(chain[rootChain.size() - 1], root)
            || !configaccess::samePath(chain[chain.size() - 2], chain[chain.size() - 3] + L"\\AppData")
            || !configaccess::samePath(full, chain[chain.size() - 2] + L"\\Local"))
        {
            reason = L"not <profiles root>\\<name>\\AppData\\Local";
        }
        else
        {
            for (const auto& component : chain)
            {
                if (!fileSystem.isPlainDirectory(component, reason))
                {
                    if (reason.empty())
                        reason = L"missing, unreadable or reparse directory: " + component;
                    return std::nullopt;
                }
            }
            return full;
        }
    }
    return std::nullopt;
}
}
