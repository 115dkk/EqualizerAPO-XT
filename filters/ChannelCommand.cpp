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

#include "ChannelCommand.h"

#include <cwctype>

#include "audio/ChannelLayout.h"

std::wstring ChannelCommand::serialize() const
{
	std::wstring result;
	for (const std::wstring& channel : channels)
	{
		if (!result.empty())
			result += L" ";
		result += channel;
	}
	return result;
}

bool ChannelCommand::parse(const std::wstring& command, const std::wstring& parameters, ChannelCommand& out)
{
	if (command != L"Channel")
		return false;

	out.channels.clear();

	// Tokenizer preserved from the engine factory: split on whitespace and
	// commas, upper-case every selector.
	std::wstring value = parameters + L" ";
	std::wstring currentWord;
	for (unsigned i = 0; i < value.length(); i++)
	{
		wchar_t c = towupper(value[i]);

		if (iswspace(c) || c == L',')
		{
			if (currentWord.length() > 0)
			{
				out.channels.push_back(currentWord);
				currentWord.clear();
			}
		}
		else
		{
			currentWord += c;
		}
	}

	return true;
}

std::vector<std::wstring> ChannelCommand::resolveSelection(const std::vector<std::wstring>& selectorTokens,
	const std::vector<std::wstring>& channelNames)
{
	std::vector<bool> selected(channelNames.size(), false);
	for (const std::wstring& token : selectorTokens)
	{
		if (token == L"ALL")
		{
			selected.assign(channelNames.size(), true);
			continue;
		}
		// allowAdditional suppresses the invalid-name log line: the Editor
		// resolves on every channel propagation, not once per config load.
		const int index = ChannelLayout::getChannelIndex(token, channelNames, true);
		if (index >= 0 && index < static_cast<int>(channelNames.size()))
			selected[static_cast<size_t>(index)] = true;
	}

	std::vector<std::wstring> selectedNames;
	for (size_t i = 0; i < channelNames.size(); i++)
	{
		if (selected[i])
			selectedNames.push_back(channelNames[i]);
	}
	return selectedNames;
}
