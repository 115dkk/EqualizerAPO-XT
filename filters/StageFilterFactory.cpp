/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "text/WideString.h"
#include <mpParser.h>
#include "services/logging/Logging.h"
#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "StageCommand.h"
#include "StageFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Stage, StageFilterFactory, L"Stage")

using std::vector;
using std::wstring;

void StageFilterFactory::initialize(FilterEngine* engine)
{
	enginePreMix = engine->isPreMix();
	engineCapture = engine->isCapture();
	enginePostMixInstalled = engine->isPostMixInstalled();

	engine->getParser()->defineConst(L"stage",
		engine->isCapture() ? L"capture" : engine->isPreMix() ? L"pre-mix" : L"post-mix");
}

FilterVector StageFilterFactory::startOfConfiguration()
{
	stageMatches = engineCapture || !enginePreMix || !enginePostMixInstalled;
	while (!stageMatchesStack.empty())
		stageMatchesStack.pop();

	return {};
}

FilterVector StageFilterFactory::startOfFile(const std::wstring& configPath)
{
	stageMatchesStack.push(stageMatches);

	return {};
}

FilterVector StageFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	StageCommand cmd;
	if (StageCommand::parse(command, parameters, cmd))
	{
		stageMatches = false;

		wstring matchingPart;
		for (const wstring& part : cmd.stages)
		{
			if (part == StageCommand::preMix)
			{
				if (!engineCapture && enginePreMix)
				{
					stageMatches = true;
					matchingPart = part;
				}
			}
			else if (part == StageCommand::postMix)
			{
				if (!engineCapture && !enginePreMix)
				{
					stageMatches = true;
					matchingPart = part;
				}
			}
			else if (part == StageCommand::capture)
			{
				if (engineCapture)
				{
					stageMatches = true;
					matchingPart = part;
				}
			}
			else
			{
				LogF(L"Unknown stage \"%s\"! Only pre-mix, post-mix and capture are supported.", part.c_str());
			}
		}

		if (stageMatches)
			TraceF(L"Matching stage \"%s\"", matchingPart.c_str());
		else
			// Log the author's text (trimmed, lower-cased) rather than the
			// canonical serialization, which would collapse repeated spaces.
			TraceF(L"Not matching stage set \"%s\"", text::toLower(text::trim(parameters)).c_str());
	}

	if (!stageMatches)
		// skip line for further factories
		command = L"";

	return {};
}

FilterVector StageFilterFactory::endOfFile(const std::wstring& configPath)
{
	stageMatches = stageMatchesStack.top();
	stageMatchesStack.pop();

	return {};
}
