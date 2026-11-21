/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace UpdateLeftovers
{
// "tmp_" followed by exactly 16 ASCII letters/digits.
bool isTempEntryName(std::wstring_view name);

// rootLeafName (compared case-insensitively) followed by '_' or '.', followed
// by exactly 16 ASCII letters/digits. An empty rootLeafName never matches.
bool isRollbackRootName(std::wstring_view rootLeafName, std::wstring_view name);

// Entries touched more recently than this are left alone: a download (delta
// base extraction in VelopackTemp), an apply or a Setup run may be using them
// right now. "Touched" is the latest of the directory's creation, write and
// change times; the change time moves when the directory is renamed, which
// is how Velopack turns an old install into a backup.
inline constexpr std::chrono::seconds kMinimumAge = std::chrono::hours(1);

// Absolute paths of the leftover directories for this install root.
// Returns empty unless installRoot is an existing directory that contains
// the file Update.exe.
std::vector<std::wstring> find(const std::wstring& installRoot,
	std::chrono::seconds minimumAge = kMinimumAge);

struct SweepResult
{
	size_t found = 0;    // leftover directories found
	size_t removed = 0;  // of those, removed completely
};

// find() + remove each one. Never throws.
SweepResult sweep(const std::wstring& installRoot,
	std::chrono::seconds minimumAge = kMinimumAge);
}
