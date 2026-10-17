/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

#include "engine/IFilterFactory.h"

class FilterEngine;

class NewChannelFilterFactory : public IFilterFactory
{
public:
	LineOutcome createFilter(const std::wstring& configPath, std::wstring& command,
		std::wstring& parameters) override;
};
