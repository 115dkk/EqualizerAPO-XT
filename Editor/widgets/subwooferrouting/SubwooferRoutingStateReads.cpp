/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SubwooferRoutingStateReads.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace subwooferroutingeditor
{
const subroute::Path* findPath(
	const subroute::SubwooferRoutingState& state,
	const std::string& id)
{
	const auto path = std::find_if(state.paths.begin(), state.paths.end(),
		[&id](const subroute::Path& candidate)
		{
			return candidate.id == id;
		});

	return path == state.paths.end() ? nullptr : &*path;
}

subroute::Path* findPath(
	subroute::SubwooferRoutingState& state,
	const std::string& id)
{
	const auto path = std::find_if(state.paths.begin(), state.paths.end(),
		[&id](const subroute::Path& candidate)
		{
			return candidate.id == id;
		});

	return path == state.paths.end() ? nullptr : &*path;
}

const subroute::Path* sourceLfePath(
	const subroute::SubwooferRoutingState& state)
{
	const auto path = std::find_if(state.paths.begin(), state.paths.end(),
		[](const subroute::Path& candidate)
		{
			return candidate.kind == subroute::PathKind::SourceLfe;
		});

	return path == state.paths.end() ? nullptr : &*path;
}

const subroute::BiquadFilter* firstBiquad(
	const subroute::Path& path,
	subroute::BiquadType type)
{
	for (const subroute::PathStage& stage : path.chain)
	{
		const subroute::BiquadStage* biquad =
			std::get_if<subroute::BiquadStage>(&stage);
		if (biquad != nullptr && biquad->filter.type == type)
			return &biquad->filter;
	}

	return nullptr;
}

std::optional<double> groupHighPass(
	const subroute::SubwooferRoutingState& state,
	const subroute::SpeakerGroup& group)
{
	for (const std::string& pathId : group.mainPathIds)
	{
		const subroute::Path* path = findPath(state, pathId);
		if (path == nullptr)
			continue;

		const subroute::BiquadFilter* filter =
			firstBiquad(*path, subroute::BiquadType::HighPass);
		if (filter != nullptr)
			return filter->frequencyHz;
	}

	return std::nullopt;
}

std::optional<subroute::CrossoverRecipe> groupRecipe(
	const subroute::SubwooferRoutingState& state,
	const subroute::SpeakerGroup& group)
{
	for (const std::string& pathId : group.mainPathIds)
	{
		const subroute::Path* path = findPath(state, pathId);
		if (path == nullptr)
			continue;
		return subroute::recognizeCrossover(*path,
			subroute::BiquadType::HighPass);
	}
	return std::nullopt;
}

std::optional<double> groupDelayMs(
	const subroute::SubwooferRoutingState& state,
	const subroute::SpeakerGroup& group)
{
	for (const std::string& pathId : group.mainPathIds)
	{
		const subroute::Path* path = findPath(state, pathId);
		if (path == nullptr)
			continue;
		return pathDelayMs(*path);
	}
	return std::nullopt;
}

std::optional<double> pathLowPass(const subroute::Path& path)
{
	const subroute::BiquadFilter* filter =
		firstBiquad(path, subroute::BiquadType::LowPass);
	if (filter == nullptr)
		return std::nullopt;

	return filter->frequencyHz;
}

bool pathPolarity(const subroute::Path& path)
{
	for (const subroute::PathStage& stage : path.chain)
	{
		const subroute::PolarityStage* polarity =
			std::get_if<subroute::PolarityStage>(&stage);
		if (polarity != nullptr)
			return polarity->inverted;
	}

	return false;
}

double pathDelayMs(const subroute::Path& path)
{
	for (const subroute::PathStage& stage : path.chain)
	{
		const subroute::DelayStage* delay =
			std::get_if<subroute::DelayStage>(&stage);
		if (delay != nullptr)
			return delay->milliseconds;
	}

	return 0.0;
}

double sourceLfeEffectiveGainDb(const subroute::Path& path)
{
	double gainDb = path.preGainDb + path.postGainDb;
	if (!path.sourceMix.empty())
	{
		const double gain = std::abs(
			path.sourceMix.front().gainLinear);
		if (gain > 0.0)
			gainDb += 20.0 * std::log10(gain);
	}

	for (const subroute::PathStage& stage : path.chain)
	{
		const subroute::GainStage* gain =
			std::get_if<subroute::GainStage>(&stage);
		if (gain != nullptr)
			gainDb += gain->gainDb;
	}
	return gainDb;
}

double sourceLfeAdjustmentDb(const subroute::Path& path)
{
	return path.preGainDb;
}

bool isIssue246Preset(const subroute::PresetDescriptor& preset)
{
	return preset.id == subroute::kIssue246FrontRear41PresetId;
}
}
