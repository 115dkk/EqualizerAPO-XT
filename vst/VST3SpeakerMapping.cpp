/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "stdafx.h"
#include "VST3SpeakerMapping.h"

using namespace std;
using namespace Steinberg::Vst;

namespace
{
	bool channelNamesEqual(const vector<wstring>& channelNames,
		initializer_list<const wchar_t*> expected)
	{
		if (channelNames.size() != expected.size())
			return false;

		size_t index = 0;
		for (const wchar_t* name : expected)
		{
			if (channelNames[index++] != name)
				return false;
		}
		return true;
	}

	void appendArrangementCandidate(SpeakerArrangement arrangement,
		vector<SpeakerArrangement>& candidates)
	{
		if (find(candidates.begin(), candidates.end(), arrangement) == candidates.end())
			candidates.push_back(arrangement);
	}
}

vector<wstring> vst3speakers::channelNamesForLayout(VST3BusLayout layout,
	const vector<wstring>& automaticChannelNames)
{
	return layout == VST3BusLayout::Auto
		? automaticChannelNames : vst3BusLayoutChannelNames(layout);
}

vector<SpeakerArrangement> vst3speakers::semanticArrangementCandidates(
	const vector<wstring>& channelNames)
{
	if (channelNamesEqual(channelNames, {L"L", L"R"}))
		return {SpeakerArr::kStereo};
	if (channelNamesEqual(channelNames, {L"L", L"R", L"RL", L"RR"})
		|| channelNamesEqual(channelNames, {L"L", L"R", L"SL", L"SR"}))
	{
		return {SpeakerArr::k40Music, SpeakerArr::k40Cine};
	}
	if (channelNamesEqual(channelNames, {L"L", L"R", L"LFE", L"RL", L"RR"})
		|| channelNamesEqual(channelNames, {L"L", L"R", L"LFE", L"SL", L"SR"}))
	{
		return {SpeakerArr::k41Music, SpeakerArr::k41Cine};
	}
	if (channelNamesEqual(channelNames, {L"L", L"R", L"C", L"RL", L"RR"})
		|| channelNamesEqual(channelNames, {L"L", L"R", L"C", L"SL", L"SR"}))
	{
		return {SpeakerArr::k50};
	}
	if (channelNamesEqual(channelNames, {L"L", L"R", L"C", L"LFE", L"RL", L"RR"}))
		return {SpeakerArr::k51};
	if (channelNamesEqual(channelNames, {L"L", L"R", L"C", L"LFE", L"RC", L"SL", L"SR"}))
		return {SpeakerArr::k61Cine, SpeakerArr::k61Music};
	if (channelNamesEqual(channelNames,
		{L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"}))
	{
		return {SpeakerArr::k71Music, SpeakerArr::k71Cine};
	}
	if (channelNamesEqual(channelNames,
		{L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR", L"TSL", L"TSR"}))
	{
		return {SpeakerArr::k71_2, SpeakerArr::k71_2_TF};
	}
	if (channelNamesEqual(channelNames,
		{L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR",
			L"TFL", L"TFR", L"TRL", L"TRR"}))
	{
		return {SpeakerArr::k71_4};
	}
	return {};
}

vector<SpeakerArrangement> vst3speakers::arrangementCandidatesForChannelCount(
	int channelCount, const vector<wstring>& channelNames)
{
	vector<SpeakerArrangement> candidates = semanticArrangementCandidates(channelNames);

	// Count-based candidates stay after semantic candidates and preserve the
	// existing Windows-mask-first ordering.
	switch (channelCount)
	{
	case 1:
		appendArrangementCandidate(SpeakerArr::kMono, candidates);
		break;
	case 2:
		appendArrangementCandidate(SpeakerArr::kStereo, candidates);
		break;
	case 4:
		appendArrangementCandidate(SpeakerArr::k40Music, candidates);
		appendArrangementCandidate(SpeakerArr::k40Cine, candidates);
		break;
	case 5:
		appendArrangementCandidate(SpeakerArr::k50, candidates);
		break;
	case 6:
		appendArrangementCandidate(SpeakerArr::k51, candidates);
		break;
	case 7:
		appendArrangementCandidate(SpeakerArr::k61Cine, candidates);
		break;
	case 8:
		appendArrangementCandidate(SpeakerArr::k71Music, candidates);
		appendArrangementCandidate(SpeakerArr::k71Cine, candidates);
		break;
	case 10:
		appendArrangementCandidate(SpeakerArr::k71_2, candidates);
		break;
	case 12:
		appendArrangementCandidate(SpeakerArr::k71_4, candidates);
		break;
	}
	return candidates;
}

vector<SpeakerArrangement> vst3speakers::arrangementCandidatesForLayout(
	VST3BusLayout layout, int automaticChannelCount,
	const vector<wstring>& channelNames, SpeakerArrangement currentArrangement)
{
	if (layout == VST3BusLayout::Auto)
	{
		vector<SpeakerArrangement> candidates = arrangementCandidatesForChannelCount(
			max(1, automaticChannelCount), channelNames);
		if (currentArrangement != SpeakerArr::kEmpty)
			appendArrangementCandidate(currentArrangement, candidates);
		return candidates;
	}

	vector<SpeakerArrangement> candidates;
	switch (layout)
	{
	case VST3BusLayout::Mono:
		appendArrangementCandidate(SpeakerArr::kMono, candidates);
		break;
	case VST3BusLayout::Stereo:
		appendArrangementCandidate(SpeakerArr::kStereo, candidates);
		break;
	case VST3BusLayout::Surround40:
		appendArrangementCandidate(SpeakerArr::k40Music, candidates);
		appendArrangementCandidate(SpeakerArr::k40Cine, candidates);
		break;
	case VST3BusLayout::Surround41:
		appendArrangementCandidate(SpeakerArr::k41Music, candidates);
		appendArrangementCandidate(SpeakerArr::k41Cine, candidates);
		break;
	case VST3BusLayout::Surround50:
		appendArrangementCandidate(SpeakerArr::k50, candidates);
		break;
	case VST3BusLayout::Surround51:
		appendArrangementCandidate(SpeakerArr::k51, candidates);
		break;
	case VST3BusLayout::Surround61:
		appendArrangementCandidate(SpeakerArr::k61Cine, candidates);
		appendArrangementCandidate(SpeakerArr::k61Music, candidates);
		break;
	case VST3BusLayout::Surround71:
		appendArrangementCandidate(SpeakerArr::k71Music, candidates);
		appendArrangementCandidate(SpeakerArr::k71Cine, candidates);
		break;
	case VST3BusLayout::Surround712:
		appendArrangementCandidate(SpeakerArr::k71_2, candidates);
		appendArrangementCandidate(SpeakerArr::k71_2_TF, candidates);
		break;
	case VST3BusLayout::Surround714:
		appendArrangementCandidate(SpeakerArr::k71_4, candidates);
		break;
	case VST3BusLayout::Auto:
		break;
	}
	return candidates;
}

bool vst3speakers::arrangementMatchesLayout(SpeakerArrangement arrangement,
	VST3BusLayout layout)
{
	const vector<SpeakerArrangement> candidates = arrangementCandidatesForLayout(
		layout, 0, {}, SpeakerArr::kEmpty);
	return find(candidates.begin(), candidates.end(), arrangement) != candidates.end();
}

bool vst3speakers::buildChannelMapping(SpeakerArrangement arrangement,
	const vector<wstring>& channelNames, vector<int>& mapping)
{
	const int channelCount = arrangement != SpeakerArr::kEmpty
		? SpeakerArr::getChannelCount(arrangement) : 0;
	mapping.resize(max(0, channelCount));
	for (int i = 0; i < channelCount; i++)
		mapping[i] = i;

	if (channelCount <= 0 || channelNames.size() != static_cast<size_t>(channelCount))
		return false;

	const vector<SpeakerArrangement> semanticCandidates = semanticArrangementCandidates(channelNames);
	if (find(semanticCandidates.begin(), semanticCandidates.end(), arrangement) == semanticCandidates.end())
		return false;

	const bool hasRearPair = find(channelNames.begin(), channelNames.end(), L"RL") != channelNames.end()
		&& find(channelNames.begin(), channelNames.end(), L"RR") != channelNames.end();
	const bool hasSidePair = find(channelNames.begin(), channelNames.end(), L"SL") != channelNames.end()
		&& find(channelNames.begin(), channelNames.end(), L"SR") != channelNames.end();

	vector<int> proposedMapping(channelCount);
	for (int i = 0; i < channelCount; i++)
		proposedMapping[i] = i;
	vector<bool> usedBusSlots(channelCount, false);
	for (int eapoSlot = 0; eapoSlot < channelCount; eapoSlot++)
	{
		const wstring& name = channelNames[eapoSlot];
		Speaker speaker = 0;
		if (name == L"L")
			speaker = kSpeakerL;
		else if (name == L"R")
			speaker = kSpeakerR;
		else if (name == L"C")
			speaker = kSpeakerC;
		else if (name == L"LFE")
			speaker = kSpeakerLfe;
		else if (name == L"RL")
			speaker = kSpeakerLs;
		else if (name == L"RR")
			speaker = kSpeakerRs;
		else if (name == L"SL")
			speaker = hasRearPair && hasSidePair ? kSpeakerSl : kSpeakerLs;
		else if (name == L"SR")
			speaker = hasRearPair && hasSidePair ? kSpeakerSr : kSpeakerRs;
		else if (name == L"RC")
			speaker = kSpeakerCs;
		else if (name == L"TFL")
			speaker = kSpeakerTfl;
		else if (name == L"TFR")
			speaker = kSpeakerTfr;
		else if (name == L"TRL")
			speaker = kSpeakerTrl;
		else if (name == L"TRR")
			speaker = kSpeakerTrr;
		else if (name == L"TSL")
			speaker = kSpeakerTsl;
		else if (name == L"TSR")
			speaker = kSpeakerTsr;
		else
			return false;

		const int busSlot = SpeakerArr::getSpeakerIndex(speaker, arrangement);
		if (busSlot < 0 || busSlot >= channelCount || usedBusSlots[busSlot])
			return false;
		proposedMapping[eapoSlot] = busSlot;
		usedBusSlots[busSlot] = true;
	}

	for (bool used : usedBusSlots)
	{
		if (!used)
			return false;
	}
	mapping = move(proposedMapping);
	return true;
}
