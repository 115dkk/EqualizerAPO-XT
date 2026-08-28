/*
	This file is part of EqualizerAPO-XT.
	Copyright (C) 2026 EqualizerAPO-XT contributors
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "engine/IFilterFactory.h"

class VelvetFilterFactory : public ParseReportingFactory
{
public:
	void initialize(FilterEngine* engine) override;
	FilterVector createFilter(const std::wstring& configPath,
		std::wstring& command, std::wstring& parameters) override;

private:
	FilterEngine* engine = nullptr;
};
