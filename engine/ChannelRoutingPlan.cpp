/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include <algorithm>
#include <utility>

#include "ChannelRoutingPlan.h"

using std::find;
using std::move;
using std::swap;
using std::vector;
using std::wstring;

void ChannelRoutingPlan::begin(vector<wstring> deviceChannels, bool previousLoadEndedInPlace)
{
	allNames = move(deviceChannels);
	currentNames = allNames;
	lastNames.clear();
	lastNewNames.clear();
	savedNames.clear();
	lastInPlaceFlag = previousLoadEndedInPlace;
}

ChannelRoutingPlan::Entry ChannelRoutingPlan::enter(bool allChannels)
{
	savedNames = currentNames;
	if (allChannels)
		currentNames = allNames;

	Entry entry;
	if (lastNames != currentNames)
	{
		entry.inChannels.resize(currentNames.size());

		size_t c = 0;
		for (const wstring& name : currentNames)
		{
			// Defensive: every currentNames entry should already be in allNames
			// (seeded from it, or a filter's own subset). If that invariant is
			// ever broken, append the name instead of storing a one-past-the-end
			// index that process() would read out of bounds; the appended channel
			// reads the zero-filled virtual range (silence). Mirrors the
			// outChannels handling in leave().
			entry.inChannels[c++] = placeByName(name);
		}
	}

	lastNames = currentNames;
	entry.initializeWith = currentNames;
	return entry;
}

vector<size_t> ChannelRoutingPlan::leave(const vector<wstring>& newChannelNames, bool inPlace, bool selectChannels)
{
	vector<size_t> outChannels;
	if (!(inPlace && lastInPlaceFlag && lastNewNames == newChannelNames))
	{
		outChannels.resize(newChannelNames.size());

		size_t c = 0;
		for (const wstring& name : newChannelNames)
			outChannels[c++] = placeByName(name);
	}

	lastNewNames = newChannelNames;
	lastInPlaceFlag = inPlace;
	if (!lastInPlaceFlag)
		swap(lastNames, lastNewNames);

	if (selectChannels)
		currentNames = newChannelNames;
	else
		currentNames = savedNames;

	return outChannels;
}

const vector<wstring>& ChannelRoutingPlan::allChannelNames() const
{
	return allNames;
}

const vector<wstring>& ChannelRoutingPlan::currentChannelNames() const
{
	return currentNames;
}

void ChannelRoutingPlan::setCurrentChannelNames(vector<wstring> names)
{
	currentNames = move(names);
}

bool ChannelRoutingPlan::lastInPlace() const
{
	return lastInPlaceFlag;
}

size_t ChannelRoutingPlan::placeByName(const wstring& name)
{
	vector<wstring>::const_iterator pos = find(allNames.cbegin(), allNames.cend(), name);
	if (pos != allNames.cend())
		return pos - allNames.cbegin();

	allNames.push_back(name);
	return allNames.size() - 1;
}
