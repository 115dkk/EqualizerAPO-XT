/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include <algorithm>
#include <vector>

#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "NewChannelCommand.h"
#include "NewChannelFilter.h"
#include "NewChannelFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::NewChannel, NewChannelFilterFactory, L"NewChannel")

using std::find;
using std::vector;
using std::wstring;

void NewChannelFilterFactory::initialize(FilterEngine* engine)
{
	ParseReportingFactory::initialize(engine);
	this->engine = engine;
}

FilterVector NewChannelFilterFactory::createFilter(const wstring& configPath, wstring& command,
	wstring& parameters)
{
	NewChannelCommand cmd;
	if (!NewChannelCommand::parse(command, parameters, cmd))
		return {};

	if (cmd.names.empty())
		return reportParseError(command, L"expected at least one channel name, as in \"NewChannel: VC\"");

	const wstring problem = NewChannelCommand::validate(cmd.names,
		engine != nullptr ? engine->deviceChannelNames() : vector<wstring>());
	if (!problem.empty())
		return reportParseError(command, problem);

	vector<wstring> alreadyDeclared;
	if (engine != nullptr)
	{
		const vector<wstring>& channelNames = engine->loadingChannelNames();
		for (const wstring& name : cmd.names)
		{
			if (find(channelNames.cbegin(), channelNames.cend(), name) != channelNames.cend())
				alreadyDeclared.push_back(name);
		}
	}

	return singleFilter(makeFilter<NewChannelFilter>(cmd.names, alreadyDeclared));
}
