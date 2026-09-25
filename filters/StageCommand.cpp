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

#include "StageCommand.h"


using std::wstring;

bool StageCommand::contains(const wstring& stage) const
{
	for (const wstring& s : stages)
		if (s == stage)
			return true;
	return false;
}

bool StageCommand::matchesByDefault(bool instancePreMix, bool instanceCapture, bool postMixInstalled)
{
	return instanceCapture || !instancePreMix || !postMixInstalled;
}

bool StageCommand::matches(bool instancePreMix, bool instanceCapture, wstring* matchingStage) const
{
	// Matching loop preserved from the engine factory: every selector is
	// checked, so the last matching one is the one reported.
	bool result = false;
	for (const wstring& part : stages)
	{
		const bool partMatches = part == preMix ? !instanceCapture && instancePreMix
			: part == postMix ? !instanceCapture && !instancePreMix
			: part == capture ? instanceCapture
			: false;
		if (partMatches)
		{
			result = true;
			if (matchingStage != nullptr)
				*matchingStage = part;
		}
	}
	return result;
}

bool StageCommand::isKnownStage(const wstring& stage)
{
	return stage == preMix || stage == postMix || stage == capture;
}

wstring StageCommand::serialize() const
{
	wstring result;
	for (const wstring& stage : stages)
	{
		if (!result.empty())
			result += L" ";
		result += stage;
	}
	return result;
}

bool StageCommand::parse(const wstring& command, const wstring& parameters, StageCommand& out)
{
	if (command != L"Stage")
		return false;

	// Tokenizer preserved from the engine factory: trim, lower-case, split on
	// single spaces (empty parts are skipped, other whitespace is not split).
	out.stages = text::split(text::toLower(text::trim(parameters)), L' ');

	return true;
}
