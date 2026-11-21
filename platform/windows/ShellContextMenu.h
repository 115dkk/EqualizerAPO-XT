/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2026  115dkk

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace winutil
{
	enum class ShellMenuResult
	{
		Unavailable,
		Dismissed,
		Invoked
	};

	// The caller must have initialized OLE on the owner's STA thread.
	// All paths must be absolute and have the same parent folder. Blocks until
	// the menu is dismissed. screenPoint is in physical screen pixels.
	ShellMenuResult showShellItemMenu(HWND owner, const std::vector<std::wstring>& paths, POINT screenPoint);

	// The folder-background menu, without an Explorer view. View-dependent
	// commands are not supported. The same thread and coordinate rules apply.
	ShellMenuResult showShellFolderBackgroundMenu(HWND owner, const std::wstring& folder, POINT screenPoint);

	namespace detail
	{
		// Test-only probes: build and inspect the production menu without tracking
		// it or invoking commands. Initialize OLE before calling these functions.
		// Counts are top-level items; rename inspection includes existing submenus.
		bool buildShellItemMenuForTest(const std::vector<std::wstring>& paths, int& itemCount, bool& hasRenameVerb);
		bool buildShellFolderBackgroundMenuForTest(const std::wstring& folder, int& itemCount, bool& hasRenameVerb);
	}
}
