/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

// The channel bookkeeping FilterEngine::addFilters does for every filter
// (audit #348 F1). It carries the producing side of the channel-inheritance
// contract documented at FilterInfo in FilterConfiguration.h: an empty
// inChannels means "the buffers the previous filter left", an empty
// outChannels "in place over the same names as the previous in-place filter".
// Pure: no filters, no I/O, so ChannelInheritanceTests drives it directly.
class ChannelRoutingPlan
{
public:
	// Start of a load: every channel of the device, nothing placed yet.
	// lastInPlace carries across loads on purpose (see loadConfig).
	void begin(std::vector<std::wstring> deviceChannels, bool previousLoadEndedInPlace);

	// Before a filter's initialize(): the names it is initialized with and its
	// input indices (empty when they are the previous filter's).
	struct Entry
	{
		std::vector<std::wstring> initializeWith;
		std::vector<size_t> inChannels;
	};
	Entry enter(bool allChannels);

	// After initialize() returned newChannelNames: the output indices (empty
	// when in place over the same names), new names appended to the channel
	// list by name, and the names the next filter sees.
	std::vector<size_t> leave(const std::vector<std::wstring>& newChannelNames, bool inPlace, bool selectChannels);

	const std::vector<std::wstring>& allChannelNames() const;
	// Include recursion saves and restores the current selection.
	const std::vector<std::wstring>& currentChannelNames() const;
	void setCurrentChannelNames(std::vector<std::wstring> names);
	bool lastInPlace() const;

private:
	// Index of name in allNames, appending it when it is not there yet.
	size_t placeByName(const std::wstring& name);

	std::vector<std::wstring> allNames;
	std::vector<std::wstring> currentNames;
	std::vector<std::wstring> lastNames;
	std::vector<std::wstring> lastNewNames;
	// The selection enter() found, restored by leave() unless the filter
	// selects channels itself (a getAllChannels filter widens it only for
	// its own initialize()).
	std::vector<std::wstring> savedNames;
	bool lastInPlaceFlag = false;
};
