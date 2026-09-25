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
	// The SDK side of vst3BusLayoutTable (audit #348 TD-48): for each layout,
	// in the same order, the speaker arrangements that stand for it.
	//   - layout: proposed, in this order, when the config names the layout,
	//     and accepted as that layout when the plug-in reports one of them.
	//   - byCount: proposed after the semantic candidates when only a channel
	//     count is known. A subset: the Windows-mask order the host has
	//     always used, so 4.1 adds nothing to a 5-channel count (5.0 does).
	//   - semantic: the layout's channel names identify it as a whole. Mono
	//     is left out, so a lone C never proposes kMono ahead of the count.
	//   - sideAlias: the names also match with SL/SR in place of RL/RR, the
	//     spelling of a Windows side-only mask.
	struct ArrangementRow
	{
		VST3BusLayout layout = VST3BusLayout::Auto;
		vector<SpeakerArrangement> byLayout;
		vector<SpeakerArrangement> byCount;
		bool semantic = false;
		bool sideAlias = false;
	};

	const vector<ArrangementRow>& arrangementTable()
	{
		static const vector<ArrangementRow> rows = {
			{VST3BusLayout::Auto, {}, {}, false, false},
			{VST3BusLayout::Mono, {SpeakerArr::kMono}, {SpeakerArr::kMono}, false, false},
			{VST3BusLayout::Stereo, {SpeakerArr::kStereo}, {SpeakerArr::kStereo}, true, false},
			{VST3BusLayout::Surround40, {SpeakerArr::k40Music, SpeakerArr::k40Cine},
				{SpeakerArr::k40Music, SpeakerArr::k40Cine}, true, true},
			{VST3BusLayout::Surround41, {SpeakerArr::k41Music, SpeakerArr::k41Cine}, {}, true, true},
			{VST3BusLayout::Surround50, {SpeakerArr::k50}, {SpeakerArr::k50}, true, true},
			{VST3BusLayout::Surround51, {SpeakerArr::k51}, {SpeakerArr::k51}, true, false},
			{VST3BusLayout::Surround61, {SpeakerArr::k61Cine, SpeakerArr::k61Music},
				{SpeakerArr::k61Cine}, true, false},
			{VST3BusLayout::Surround71, {SpeakerArr::k71Music, SpeakerArr::k71Cine},
				{SpeakerArr::k71Music, SpeakerArr::k71Cine}, true, false},
			{VST3BusLayout::Surround712, {SpeakerArr::k71_2, SpeakerArr::k71_2_TF},
				{SpeakerArr::k71_2}, true, false},
			{VST3BusLayout::Surround714, {SpeakerArr::k71_4}, {SpeakerArr::k71_4}, true, false}
		};
		return rows;
	}

	const ArrangementRow& arrangementRow(VST3BusLayout layout)
	{
		const vector<ArrangementRow>& rows = arrangementTable();
		const size_t index = static_cast<size_t>(layout);
		return index < rows.size() ? rows[index] : rows[0];
	}

	bool channelNamesEqual(const vector<wstring>& channelNames, VST3BusLayout layout, bool sideSpelling)
	{
		const vector<wstring> expected = vst3BusLayoutChannelNames(layout);
		if (channelNames.size() != expected.size())
			return false;

		for (size_t index = 0; index < expected.size(); index++)
		{
			wstring name = expected[index];
			if (sideSpelling && name == L"RL")
				name = L"SL";
			else if (sideSpelling && name == L"RR")
				name = L"SR";
			if (channelNames[index] != name)
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
	for (const ArrangementRow& row : arrangementTable())
	{
		if (!row.semantic)
			continue;
		if (channelNamesEqual(channelNames, row.layout, false)
			|| (row.sideAlias && channelNamesEqual(channelNames, row.layout, true)))
		{
			return row.byLayout;
		}
	}
	return {};
}

vector<SpeakerArrangement> vst3speakers::arrangementCandidatesForChannelCount(
	int channelCount, const vector<wstring>& channelNames)
{
	vector<SpeakerArrangement> candidates = semanticArrangementCandidates(channelNames);

	// Count-based candidates stay after semantic candidates and preserve the
	// existing Windows-mask-first ordering.
	for (const ArrangementRow& row : arrangementTable())
	{
		if (row.layout == VST3BusLayout::Auto || vst3BusLayoutChannelCount(row.layout) != channelCount)
			continue;
		for (SpeakerArrangement arrangement : row.byCount)
			appendArrangementCandidate(arrangement, candidates);
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

	return arrangementRow(layout).byLayout;
}

bool vst3speakers::arrangementMatchesLayout(SpeakerArrangement arrangement,
	VST3BusLayout layout)
{
	const vector<SpeakerArrangement> candidates = arrangementCandidatesForLayout(
		layout, 0, {}, SpeakerArr::kEmpty);
	return find(candidates.begin(), candidates.end(), arrangement) != candidates.end();
}

optional<VST3BusLayout> vst3speakers::layoutOfArrangement(SpeakerArrangement arrangement)
{
	for (const VST3BusLayoutDefinition& definition : vst3ExplicitBusLayouts())
	{
		const vector<SpeakerArrangement>& candidates = arrangementRow(definition.layout).byLayout;
		if (find(candidates.begin(), candidates.end(), arrangement) != candidates.end())
			return definition.layout;
	}
	return nullopt;
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
