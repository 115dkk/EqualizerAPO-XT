/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "VelvetFilterFactory.h"

#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "VelvetCommand.h"
#include "VelvetFilter.h"

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_FACTORY(FilterFactoryPriority::Velvet,
	VelvetFilterFactory, L"Velvet")

void VelvetFilterFactory::initialize(FilterEngine* engine)
{
	ParseReportingFactory::initialize(engine);
	this->engine = engine;
}

FilterVector VelvetFilterFactory::createFilter(const std::wstring& configPath,
	std::wstring& command, std::wstring& parameters)
{
	(void)configPath;
	if (command != L"Velvet")
		return {};

	VelvetCommand parsed;
	std::wstring error;
	if (!VelvetCommand::parse(command, parameters, parsed, &error))
		return reportParseError(command, error);

	if (engine != nullptr && engine->isAnalysisMode()
		&& parsed.parameters.dynamic)
	{
		parsed.parameters.dynamic = false;
		engine->markFrozenDynamicAnalysis();
	}
	return singleFilter(makeFilter<VelvetFilter>(parsed.parameters));
}
