/*
	This file is part of EqualizerAPO-XT.
	Copyright (C) 2026 EqualizerAPO-XT contributors
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

class ConvolutionFilePath
{
public:
	static std::wstring normalizeParameter(const std::wstring& parameters);
	static std::wstring resolve(const std::wstring& configPath, const std::wstring& parameters);
};
