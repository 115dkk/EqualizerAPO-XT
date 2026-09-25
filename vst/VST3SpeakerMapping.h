/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <string>
#include <vector>

#include "pluginterfaces/vst/vstspeaker.h"
#include "VST3BusLayout.h"

namespace vst3speakers
{
	std::vector<std::wstring> channelNamesForLayout(VST3BusLayout layout,
		const std::vector<std::wstring>& automaticChannelNames);
	std::vector<Steinberg::Vst::SpeakerArrangement> semanticArrangementCandidates(
		const std::vector<std::wstring>& channelNames);
	std::vector<Steinberg::Vst::SpeakerArrangement> arrangementCandidatesForChannelCount(
		int channelCount, const std::vector<std::wstring>& channelNames);
	std::vector<Steinberg::Vst::SpeakerArrangement> arrangementCandidatesForLayout(
		VST3BusLayout layout, int automaticChannelCount,
		const std::vector<std::wstring>& channelNames,
		Steinberg::Vst::SpeakerArrangement currentArrangement);
	bool arrangementMatchesLayout(Steinberg::Vst::SpeakerArrangement arrangement,
		VST3BusLayout layout);
	bool buildChannelMapping(Steinberg::Vst::SpeakerArrangement arrangement,
		const std::vector<std::wstring>& channelNames, std::vector<int>& mapping);
}
