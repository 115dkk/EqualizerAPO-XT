/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

#include "MultiConvolutionCommand.h"

#include "helpers/StringHelper.h"

const std::wstring& MultiConvolutionCommand::serialize() const
{
	serialized = outputChannel + L" " + path;
	return serialized;
}

bool MultiConvolutionCommand::parse(const std::wstring& command, const std::wstring& parameters, MultiConvolutionCommand& out)
{
	if (command != L"MultiConvolution")
		return false;

	// The output channel is the first whitespace-delimited token; the remainder
	// (trimmed) is the IR path. A line with no space separates nothing, so there
	// is no path and the line is rejected.
	std::wstring trimmed = StringHelper::trim(parameters);
	size_t space = trimmed.find_first_of(L" \t");
	if (space == std::wstring::npos)
		return false;

	out.outputChannel = trimmed.substr(0, space);
	out.path = StringHelper::trim(trimmed.substr(space + 1));
	if (out.outputChannel.empty() || out.path.empty())
		return false;

	return true;
}
