/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include <algorithm>
#include <cwctype>

#include "audio/ChannelLayout.h"
#include "text/WideString.h"
#include "NewChannelCommand.h"

using std::find;
using std::vector;
using std::wstring;

wstring NewChannelCommand::serialize() const
{
	return text::join(names, L" ");
}

bool NewChannelCommand::parse(const wstring& command, const wstring& parameters, NewChannelCommand& out)
{
	if (command != L"NewChannel")
		return false;

	out.names.clear();
	wstring currentName;
	const wstring value = parameters + L" ";
	for (wchar_t character : value)
	{
		character = towupper(character);
		if (iswspace(character) || character == L',')
		{
			if (!currentName.empty())
			{
				if (find(out.names.cbegin(), out.names.cend(), currentName) == out.names.cend())
					out.names.push_back(currentName);
				currentName.clear();
			}
		}
		else
		{
			currentName += character;
		}
	}

	return true;
}

NewChannelCommand::Problem NewChannelCommand::judgeName(const wstring& name,
	const vector<wstring>& deviceChannels)
{
	if (name.empty() || iswdigit(name[0]))
		return Problem::StartsWithDigit;
	if (name == L"ALL")
		return Problem::ReservedAll;
	if (name.find_first_of(L"=*+-.`") != wstring::npos)
		return Problem::SeparatorCharacter;
	if (ChannelLayout::getChannelIndex(name, deviceChannels, true) != -1)
		return Problem::DeviceChannel;
	return Problem::None;
}

wstring NewChannelCommand::describe(Problem problem)
{
	switch (problem)
	{
	case Problem::StartsWithDigit:
		return L"starts with a digit";
	case Problem::ReservedAll:
		return L"ALL is reserved";
	case Problem::SeparatorCharacter:
		return L"contains a character Copy uses as a separator";
	case Problem::DeviceChannel:
		return L"is a device channel";
	case Problem::None:
	default:
		return L"";
	}
}

wstring NewChannelCommand::validate(const vector<wstring>& names, const vector<wstring>& deviceChannels)
{
	vector<wstring> problems;
	for (const wstring& name : names)
	{
		const Problem problem = judgeName(name, deviceChannels);
		if (problem != Problem::None)
			problems.push_back(name + L" (" + describe(problem) + L")");
	}

	return problems.empty() ? L"" : L"invalid channel name(s): " + text::join(problems, L", ");
}

vector<wstring> NewChannelCommand::extendSelection(vector<wstring> selection, const vector<wstring>& names)
{
	for (const wstring& name : names)
	{
		if (find(selection.cbegin(), selection.cend(), name) == selection.cend())
			selection.push_back(name);
	}
	return selection;
}
