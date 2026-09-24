/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SubwooferRoutingDefaults.h"

#include <algorithm>
#include <optional>

#include <QString>

#include "SubwooferRouting/Compiler.h"
#include "filters/subwooferRouting/SubwooferRoutingCommand.h"

namespace
{
constexpr double kDefaultCrossoverHz = 80.0;
constexpr double kButterworthQ = 0.7071067811865476;

subroute::Path makePath(const std::string& id,
	subroute::PathKind kind,
	const std::vector<subroute::SourceMixTerm>& sourceMix,
	std::optional<subroute::BiquadType> crossoverType)
{
	subroute::Path path;
	path.id = id;
	path.kind = kind;
	path.sourceMix = sourceMix;
	path.chain.push_back(subroute::PolarityStage{false});
	if (crossoverType.has_value())
	{
		subroute::BiquadFilter filter;
		filter.type = *crossoverType;
		filter.frequencyHz = kDefaultCrossoverHz;
		filter.q = kButterworthQ;
		filter.gainDb = 0.0;
		path.chain.push_back(subroute::BiquadStage{filter});
	}
	path.chain.push_back(subroute::DelayStage{0.0});
	path.chain.push_back(subroute::EqualizerSlotsStage{});
	return path;
}

std::vector<std::string> usableChannelIds(
	const std::vector<std::wstring>& channels)
{
	std::vector<std::string> result;
	for (const std::wstring& channel : channels)
	{
		const std::string id = subwooferRoutingToUtf8(channel);
		if (!id.empty() && subroute::isValidStableId(id)
			&& std::find(result.begin(), result.end(), id) == result.end())
		{
			result.push_back(id);
		}
	}
	return result;
}

bool sameIdIgnoringCase(const std::string& id, const char* name)
{
	return QString::fromUtf8(id.data(), static_cast<int>(id.size()))
		.compare(QLatin1String(name), Qt::CaseInsensitive) == 0;
}
}

namespace subwooferroutingeditor
{
bool isLfeChannelId(const std::string& id)
{
	return sameIdIgnoringCase(id, "LFE");
}

subroute::SubwooferRoutingState buildDefaultState(
	const std::vector<std::wstring>& deviceChannels)
{
	std::vector<std::string> channels =
		usableChannelIds(deviceChannels);
	if (channels.size() < 2)
		channels = {"L", "R"};

	auto lfe = std::find_if(channels.begin(), channels.end(),
		[](const std::string& id)
		{
			return isLfeChannelId(id);
		});

	std::vector<std::string> mainChannels;
	for (const std::string& id : channels)
	{
		if (!isLfeChannelId(id))
			mainChannels.push_back(id);
	}
	if (mainChannels.size() < 2)
	{
		channels = {"L", "R"};
		mainChannels = channels;
		lfe = channels.end();
	}

	std::string left = mainChannels[0];
	std::string right = mainChannels[1];
	for (const std::string& id : mainChannels)
	{
		if (sameIdIgnoringCase(id, "L"))
			left = id;
		else if (sameIdIgnoringCase(id, "R"))
			right = id;
	}

	const bool hasLfe = lfe != channels.end();
	const std::string lfeId = hasLfe ? *lfe : std::string();

	subroute::SubwooferRoutingState state;
	for (const std::string& id : channels)
		state.layout.channels.push_back({id, id});

	state.metadata.creatingApp = "Equalizer APO XT";
	state.metadata.creatingAppVersion = "";
	state.metadata.profileName = "";
	state.headroom.mode = subroute::HeadroomMode::Auto;
	state.headroom.manualTrimDb = 0.0;

	const std::optional<subroute::BiquadType> mainCrossover =
		hasLfe
		? std::optional<subroute::BiquadType>(
			subroute::BiquadType::HighPass)
		: std::nullopt;

	state.paths.push_back(makePath("FrontLeft",
		subroute::PathKind::Main, {{left, 1.0}},
		mainCrossover));
	state.paths.push_back(makePath("FrontRight",
		subroute::PathKind::Main, {{right, 1.0}},
		mainCrossover));

	subroute::SpeakerGroup group;
	group.id = "Front";
	group.displayName = "Front";
	group.mainPathIds = {"FrontLeft", "FrontRight"};

	if (hasLfe)
	{
		state.paths.push_back(makePath("FrontBass",
			subroute::PathKind::Bass,
			{{left, 1.0}, {right, 1.0}},
			subroute::BiquadType::LowPass));
		group.bassPathId = "FrontBass";

		state.paths.push_back(makePath("SourceLFE",
			subroute::PathKind::SourceLfe,
			{{lfeId, 1.0}}, std::nullopt));
	}

	state.speakerGroups.push_back(group);

	state.outputMatrix.push_back(
		{left, subroute::OutputMode::Replace,
			{{"FrontLeft", 0.0}}});
	state.outputMatrix.push_back(
		{right, subroute::OutputMode::Replace,
			{{"FrontRight", 0.0}}});

	if (hasLfe)
	{
		state.outputMatrix.push_back(
			{lfeId, subroute::OutputMode::Replace,
				{{"FrontBass", 0.0}, {"SourceLFE", 0.0}}});
	}

	return state;
}
}
