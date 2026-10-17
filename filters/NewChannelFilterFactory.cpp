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

LineOutcome NewChannelFilterFactory::createFilter(const wstring& configPath, wstring& command,
	wstring& parameters)
{
	NewChannelCommand cmd;
	if (!NewChannelCommand::parse(command, parameters, cmd))
		return {};

	if (cmd.names.empty())
		return LineOutcome::malformed(L"expected at least one channel name, as in \"NewChannel: VC\"");

	const wstring problem = NewChannelCommand::validate(cmd.names,
		engine() != nullptr ? engine()->deviceChannelNames() : vector<wstring>());
	if (!problem.empty())
		return LineOutcome::malformed(problem);

	vector<wstring> alreadyDeclared;
	if (engine() != nullptr)
	{
		const vector<wstring>& channelNames = engine()->loadingChannelNames();
		for (const wstring& name : cmd.names)
		{
			if (find(channelNames.cbegin(), channelNames.cend(), name) != channelNames.cend())
				alreadyDeclared.push_back(name);
		}
	}

	return LineOutcome::produced(singleFilter(makeFilter<NewChannelFilter>(cmd.names, alreadyDeclared)));
}
