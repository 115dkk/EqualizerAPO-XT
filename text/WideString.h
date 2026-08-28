/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2013  Jonas Thedering
*/

#pragma once

#include <string>
#include <vector>

namespace text
{
	std::wstring replaceCharacters(const std::wstring& value, const std::wstring& characters,
		const std::wstring& replacement);
	std::wstring replaceIllegalFilenameCharacters(const std::wstring& filename);
	std::wstring toLower(const std::wstring& value);
	std::wstring toUpper(const std::wstring& value);
	std::wstring trim(const std::wstring& value);
	std::vector<std::wstring> split(const std::wstring& value, wchar_t delimiter, bool skipEmpty = true);
	std::wstring join(const std::vector<std::wstring>& values, const std::wstring& separator);
	std::vector<std::wstring> splitQuoted(const std::wstring& value, wchar_t delimiter,
		wchar_t quote = L'"');
}
