/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors
*/

#pragma once

#include <string>

namespace wintext
{
	std::wstring toWideString(const std::string& value, unsigned codePage);
	std::string toNarrowString(const std::wstring& value, unsigned codePage);
}
