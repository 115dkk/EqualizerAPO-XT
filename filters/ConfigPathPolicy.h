/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

// Whether the engine may open a file that a configuration line names.
//
// The engine runs inside audiodg.exe as LOCAL SERVICE and loads a
// configuration a standard user may edit, so a config line is the one place
// where user-written text becomes a path the service opens. Local paths of
// every shape are the product (impulse responses, plug-ins and included
// files may live anywhere on a drive). A path that leaves the local drives
// is not: a UNC share makes the service authenticate to a host the user
// chose, and the device namespace (\\.\pipe\...) is never a configuration
// file. Those are refused, with one exception: a configuration that itself
// lives on a share may reference that share, so a config folder kept on a
// NAS keeps working.
class ConfigPathPolicy
{
public:
	// path: the fully resolved path a factory is about to open.
	// configPath: the configuration file whose line named it (L"" when the
	// caller has none; then every remote path is refused).
	// Returns true when the engine may open path. On false, reason carries
	// the sentence for reportParseError.
	static bool allowsOpen(const std::wstring& path, const std::wstring& configPath, std::wstring& reason);

	// The `\\host\share` prefix (or `\\?\x`, `\\.\x`: the first two
	// components after two leading separators) of a path that starts with
	// two separators, lower-cased, backslashes only. Empty for every other
	// shape: drive-letter, relative, root-relative, drive-relative. Exposed
	// for the tests.
	static std::wstring remoteRoot(const std::wstring& path);
};
