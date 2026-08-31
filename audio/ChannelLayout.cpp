/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Ks.h>
#include <KsMedia.h>

#include "services/logging/Logging.h"
#include "audio/ChannelLayout.h"

using std::find;
using std::to_wstring;
using std::vector;
using std::wstring;

namespace
{
	constexpr std::array<std::pair<std::wstring_view, int>, 9> channelPositions{
		std::pair{ std::wstring_view{ L"L" }, SPEAKER_FRONT_LEFT },
		std::pair{ std::wstring_view{ L"R" }, SPEAKER_FRONT_RIGHT },
		std::pair{ std::wstring_view{ L"C" }, SPEAKER_FRONT_CENTER },
		std::pair{ std::wstring_view{ L"LFE" }, SPEAKER_LOW_FREQUENCY },
		std::pair{ std::wstring_view{ L"RL" }, SPEAKER_BACK_LEFT },
		std::pair{ std::wstring_view{ L"RR" }, SPEAKER_BACK_RIGHT },
		std::pair{ std::wstring_view{ L"RC" }, SPEAKER_BACK_CENTER },
		std::pair{ std::wstring_view{ L"SL" }, SPEAKER_SIDE_LEFT },
		std::pair{ std::wstring_view{ L"SR" }, SPEAKER_SIDE_RIGHT }
	};
}

int ChannelLayout::getDefaultChannelMask(int channelCount)
{
	int channelMask;

	switch (channelCount)
	{
	case 1:
		channelMask = KSAUDIO_SPEAKER_MONO;
		break;
	case 2:
		channelMask = KSAUDIO_SPEAKER_STEREO;
		break;
	case 4:
		channelMask = KSAUDIO_SPEAKER_QUAD;
		break;
	case 6:
		channelMask = KSAUDIO_SPEAKER_5POINT1_SURROUND;
		break;
	case 8:
		channelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
		break;
	default:
		channelMask = 0;
	}

	return channelMask;
}

vector<wstring> ChannelLayout::getChannelNames(int channelCount, int channelMask)
{
	vector<wstring> channelNames;
	channelNames.reserve(channelCount);
	int c = 1;
	for (int i = 0; i < 31; i++)
	{
		int channelPos = 1 << i;
		if (channelMask & channelPos)
		{
			auto it = std::find_if(channelPositions.begin(), channelPositions.end(),
				[channelPos](const auto& entry) { return entry.second == channelPos; });
			if (it != channelPositions.end())
				channelNames.emplace_back(it->first.data(), it->first.size());
			else
				channelNames.push_back(to_wstring((unsigned long long)c));
			c++;
		}
	}

	// handle channels not covered by channelMask
	for (; c <= channelCount; c++)
		channelNames.push_back(to_wstring((unsigned long long)c));

	return channelNames;
}

int ChannelLayout::getChannelIndex(std::wstring word, const std::vector<std::wstring>& channelNames, bool allowAdditional)
{
	int channelIndex = -1;

	if (iswdigit(word[0]))
	{
		channelIndex = wcstol(word.c_str(), nullptr, 10) - 1;

		if (channelIndex < 0 || channelIndex >= static_cast<int>(channelNames.size()))
		{
			LogFStatic(L"Channel number %s out of range (1 - %u)", word.c_str(), static_cast<unsigned>(channelNames.size()));
			channelIndex = -1;
		}
	}
	else
	{
		vector<wstring>::const_iterator pos = find(channelNames.begin(), channelNames.end(), word);

		if (pos == channelNames.end())
		{
			// Special handling to accept "wrong", but unambiguous positions
			if (word == L"SL")
				pos = find(channelNames.begin(), channelNames.end(), L"RL");
			else if (word == L"SR")
				pos = find(channelNames.begin(), channelNames.end(), L"RR");
			else if (word == L"RL")
				pos = find(channelNames.begin(), channelNames.end(), L"SL");
			else if (word == L"RR")
				pos = find(channelNames.begin(), channelNames.end(), L"SR");
			else if (word == L"SUB") // old channel name
				pos = find(channelNames.begin(), channelNames.end(), L"LFE");
		}

		if (pos != channelNames.end())
			channelIndex = static_cast<int>(pos - channelNames.begin());
		else if (!allowAdditional)
			LogFStatic(L"Invalid channel position %s", word.c_str());
	}

	return channelIndex;
}
