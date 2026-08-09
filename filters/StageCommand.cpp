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

#include "StageCommand.h"

#include "text/StringHelper.h"

using std::wstring;

bool StageCommand::contains(const wstring& stage) const
{
	for (const wstring& s : stages)
		if (s == stage)
			return true;
	return false;
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
	out.stages = StringHelper::split(StringHelper::toLowerCase(StringHelper::trim(parameters)), L' ');

	return true;
}
