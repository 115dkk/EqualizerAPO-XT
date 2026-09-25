/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include "ConfigFileReference.h"

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
//
// A path is judged by where it leads, not only by how it is spelled (audit
// #348 A1). A symbolic link or a junction on the way can point a local
// spelling at a share, so every component of a local path is read without
// following it, and a link's stored target is judged as a path of its own.
// A link to a share is refused that way before the service ever reaches the
// share. Runtime walks open each child relative to its held parent and retain
// every handle, leaf included, without delete sharing. A folder the engine may
// not list (a user profile ancestor, for LOCAL SERVICE) is held attributes-only
// instead; share modes do not protect its name, the held components below it
// do on NTFS (see Win32FileSystem::openChild). An unexaminable local component
// fails closed; the table-only fallback is retained for policy tests.
// Same-share references remain the explicit exception and are opened only
// after their remote root has been judged.

class ConfigPathPolicy
{
public:
	// One path component as the policy reads it, without following it.
	struct Entry
	{
		enum class Kind
		{
			// Nothing by that name. The open will fail on its own; nothing
			// further is judged.
			Missing,
			// It may exist, but it could not be read without following it.
			Unexaminable,
			// A file or a folder, or a reparse point that redirects nothing
			// (cloud placeholders, deduplicated files).
			Plain,
			// A symbolic link or a junction; target says where it points.
			Link,
			// A redirecting reparse point of a kind the engine does not follow.
			OtherLink,
		};

		Kind kind = Kind::Missing;
		// Link: the target as stored, \??\C:\x, \??\UNC\srv\share\x, or
		// relative to the link's folder.
		std::wstring target;
		bool relative = false;
	};

	// The file system as the policy reads it: Win32 in the product, a table
	// in the tests.
	class FileSystem
	{
	public:
		virtual ~FileSystem() = default;
		virtual Entry entry(const std::wstring& path) const = 0;
		// Real walks start at a root and open each child relative to the held
		// parent. Table tests need no handles and keep using entry().
		virtual bool begin(const std::wstring&) const { return true; }
		virtual bool strict() const { return false; }
		// Where an open of path arrives, spelled as GetFinalPathNameByHandleW
		// spells it (\\?\C:\x or \\?\UNC\srv\share\x); empty when path cannot
		// be opened.
		virtual std::wstring finalPath(const std::wstring& path) const = 0;
		virtual bool isNetworkDrive(wchar_t driveLetter) const = 0;
	};

	// path: the fully resolved path a factory is about to open.
	// configPath: the configuration file whose line named it (L"" when the
	// caller has none; then every remote path is refused).
	// Returns true when the engine may open path. On false, reason carries
	// the sentence for reportParseError.
	static bool allowsOpen(const std::wstring& path, const std::wstring& configPath, std::wstring& reason);
	static ConfigFileReference::Target judge(const std::wstring& path, const std::wstring& configPath, bool library = false);
	static bool allowsOpen(const std::wstring& path, const std::wstring& configPath, std::wstring& reason,
		const FileSystem& fileSystem);

	// The `\\host\share` prefix of a path spelled on a share, lower-cased,
	// backslashes only; `\\?\UNC\host\share` and `\??\UNC\host\share` have
	// the same root as `\\host\share`. A device path has its first component
	// as the root (`\\.\pipe`, `\\?\globalroot`). Empty for every local
	// shape: drive-letter, relative, root-relative, drive-relative, and the
	// verbatim drive and volume forms (`\\?\C:\x`, `\\?\Volume{...}\x`).
	// Lexical only. Exposed for the tests.
	static std::wstring remoteRoot(const std::wstring& path);

	// Links followed before a path is refused as a loop; the Win32 limit.
	static constexpr int kLinkLimit = 63;
};
