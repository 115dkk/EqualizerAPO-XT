/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

// The APO only ever connects to \\.\pipe\<name>. A name carrying a path
// separator or a dot component could steer that CreateFileW out of the pipe
// namespace (\\.\pipe\..\X), so the reader rebuilds the name from this
// allow-list and treats anything else as "no test pipe" (CodeQL #9/#10).
// DeviceTestThread writes a constant that fits; keep the two in step.
inline std::wstring sanitizeDeviceTestPipeName(const std::wstring& value)
{
	std::wstring name;
	if (value.empty() || value.size() > 128)
		return name;
	name.reserve(value.size());
	for (size_t i = 0; i < value.size(); i++)
	{
		const wchar_t c = value[i];
		const bool allowed = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z')
			|| (c >= L'a' && c <= L'z') || c == L'_' || c == L'-';
		if (!allowed)
			return std::wstring();
		name.push_back(c);
	}
	return name;
}
