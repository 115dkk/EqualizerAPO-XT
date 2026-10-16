/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "HilbertFilterFactory.h"

#include "filters/FilterFactoryRegistry.h"
#include "HilbertCommand.h"
#include "HilbertFilter.h"

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_FACTORY(FilterFactoryPriority::Hilbert,
	HilbertFilterFactory, L"Hilbert")

LineOutcome HilbertFilterFactory::createFilter(const std::wstring& configPath,
	std::wstring& command, std::wstring& parameters)
{
	(void)configPath;
	if (command != L"Hilbert")
		return {};
	HilbertCommand parsed;
	std::wstring error;
	if (!HilbertCommand::parse(command, parameters, parsed, &error))
		return LineOutcome::malformed(error);
	return LineOutcome::produced(singleFilter(makeFilter<HilbertFilter>(parsed)));
}
