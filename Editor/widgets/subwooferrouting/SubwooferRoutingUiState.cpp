/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "SubwooferRoutingUiState.h"

#include <algorithm>
#include <variant>

#include "Editor/widgets/routing/SubwooferRoutingRoutingAdapter.h"

namespace
{
subroute::PrepareSpec prepareSpecFor(
	const subroute::SubwooferRoutingState& state,
	unsigned sampleRate)
{
	subroute::PrepareSpec spec;
	spec.sampleRate = sampleRate;
	spec.maximumBlockSize = 1024;
	spec.channelLayout.reserve(state.layout.channels.size());

	for (const subroute::PhysicalChannel& channel
		: state.layout.channels)
	{
		spec.channelLayout.push_back(channel.id);
	}

	return spec;
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

/*
	Every section of the type moves together: an LR4 is two sections at one
	corner, and moving only the first would silently split the alignment.
*/
bool setSectionFrequencies(
	subroute::Path& path,
	subroute::BiquadType type,
	double frequencyHz)
{
	bool changed = false;
	for (subroute::PathStage& stage : path.chain)
	{
		subroute::BiquadStage* biquad =
			std::get_if<subroute::BiquadStage>(&stage);
		if (biquad == nullptr || biquad->filter.type != type)
			continue;

		biquad->filter.frequencyHz = frequencyHz;
		changed = true;
	}
	return changed;
}

subroute::SpeakerGroup* findGroup(
	subroute::SubwooferRoutingState& state,
	const std::string& groupId)
{
	const auto group = std::find_if(
		state.speakerGroups.begin(),
		state.speakerGroups.end(),
		[&groupId](const subroute::SpeakerGroup& candidate)
		{
			return candidate.id == groupId;
		});
	return group == state.speakerGroups.end() ? nullptr : &*group;
}

bool setPathDelay(subroute::Path& path, double milliseconds)
{
	for (subroute::PathStage& stage : path.chain)
	{
		subroute::DelayStage* delay =
			std::get_if<subroute::DelayStage>(&stage);
		if (delay == nullptr)
			continue;

		delay->milliseconds = milliseconds;
		return true;
	}
	return false;
}
}

SubwooferRoutingUiState::SubwooferRoutingUiState(
	const subroute::SubwooferRoutingState& state,
	unsigned sampleRate)
	: currentState(state),
	  deviceSampleRate(sampleRate)
{
	refreshValidation();
}

const subroute::SubwooferRoutingState&
SubwooferRoutingUiState::state() const
{
	return currentState;
}

const subroute::ValidationResult&
SubwooferRoutingUiState::validation() const
{
	return currentValidation;
}

unsigned SubwooferRoutingUiState::sampleRate() const
{
	return deviceSampleRate;
}

bool SubwooferRoutingUiState::isDirty() const
{
	return dirty;
}

std::optional<double> SubwooferRoutingUiState::computedTrimDb() const
{
	return appliedTrimDb;
}

bool SubwooferRoutingUiState::setSourceLfeGainDb(double gainDb)
{
	bool changed = false;

	for (subroute::Path& path : currentState.paths)
	{
		if (path.kind != subroute::PathKind::SourceLfe)
			continue;

		path.preGainDb = gainDb;
		changed = true;
	}

	if (!changed)
		return false;
	return commitMutation();
}

bool SubwooferRoutingUiState::setSourceLfePolarity(bool inverted)
{
	bool changed = false;

	for (subroute::Path& path : currentState.paths)
	{
		if (path.kind != subroute::PathKind::SourceLfe)
			continue;

		for (subroute::PathStage& stage : path.chain)
		{
			subroute::PolarityStage* polarity =
				std::get_if<subroute::PolarityStage>(&stage);
			if (polarity == nullptr)
				continue;

			polarity->inverted = inverted;
			changed = true;
			break;
		}
	}

	if (!changed)
		return false;
	return commitMutation();
}

bool SubwooferRoutingUiState::setSourceLfeDelayMs(double milliseconds)
{
	bool changed = false;

	for (subroute::Path& path : currentState.paths)
	{
		if (path.kind != subroute::PathKind::SourceLfe)
			continue;

		for (subroute::PathStage& stage : path.chain)
		{
			subroute::DelayStage* delay =
				std::get_if<subroute::DelayStage>(&stage);
			if (delay == nullptr)
				continue;

			delay->milliseconds = milliseconds;
			changed = true;
			break;
		}
	}

	if (!changed)
		return false;
	return commitMutation();
}

bool SubwooferRoutingUiState::setGroupHighPass(
	const std::string& groupId,
	double frequencyHz)
{
	const subroute::SpeakerGroup* group =
		findGroup(currentState, groupId);
	if (group == nullptr)
		return false;

	bool changed = false;
	for (const std::string& pathId : group->mainPathIds)
	{
		subroute::Path* path = findPath(currentState, pathId);
		if (path == nullptr)
			continue;

		changed |= setSectionFrequencies(*path,
			subroute::BiquadType::HighPass, frequencyHz);
	}

	if (!changed)
		return false;
	return commitMutation();
}

bool SubwooferRoutingUiState::setBassPathLowPass(
	const std::string& pathId,
	double frequencyHz)
{
	subroute::Path* path = findPath(currentState, pathId);
	if (path == nullptr || path->kind != subroute::PathKind::Bass)
		return false;

	if (setSectionFrequencies(*path,
		subroute::BiquadType::LowPass, frequencyHz))
	{
		return commitMutation();
	}
	return false;
}

bool SubwooferRoutingUiState::setGroupCrossover(
	const std::string& groupId,
	const subroute::CrossoverRecipe& recipe)
{
	const subroute::SpeakerGroup* group =
		findGroup(currentState, groupId);
	if (group == nullptr)
		return false;

	bool changed = false;
	for (const std::string& pathId : group->mainPathIds)
	{
		subroute::Path* path = findPath(currentState, pathId);
		if (path == nullptr)
			continue;

		changed |= subroute::applyCrossoverRecipe(*path,
			subroute::BiquadType::HighPass, recipe);
	}

	if (!changed)
		return false;
	return commitMutation();
}

bool SubwooferRoutingUiState::setBassPathCrossover(
	const std::string& pathId,
	const subroute::CrossoverRecipe& recipe)
{
	subroute::Path* path = findPath(currentState, pathId);
	if (path == nullptr || path->kind != subroute::PathKind::Bass)
		return false;

	if (subroute::applyCrossoverRecipe(*path,
		subroute::BiquadType::LowPass, recipe))
	{
		return commitMutation();
	}
	return false;
}

bool SubwooferRoutingUiState::setGroupDelayMs(
	const std::string& groupId, double milliseconds)
{
	const subroute::SpeakerGroup* group =
		findGroup(currentState, groupId);
	if (group == nullptr)
		return false;

	bool changed = false;
	for (const std::string& pathId : group->mainPathIds)
	{
		subroute::Path* path = findPath(currentState, pathId);
		if (path == nullptr)
			continue;

		changed |= setPathDelay(*path, milliseconds);
	}

	if (!changed)
		return false;
	return commitMutation();
}

bool SubwooferRoutingUiState::setPathDelayMs(
	const std::string& pathId, double milliseconds)
{
	subroute::Path* path = findPath(currentState, pathId);
	if (path == nullptr)
		return false;

	if (setPathDelay(*path, milliseconds))
		return commitMutation();
	return false;
}

bool SubwooferRoutingUiState::setPathPolarity(
	const std::string& pathId, bool inverted)
{
	subroute::Path* path = findPath(currentState, pathId);
	if (path == nullptr)
		return false;

	for (subroute::PathStage& stage : path->chain)
	{
		subroute::PolarityStage* polarity =
			std::get_if<subroute::PolarityStage>(&stage);
		if (polarity == nullptr)
			continue;

		polarity->inverted = inverted;
		return commitMutation();
	}
	return false;
}

bool SubwooferRoutingUiState::setHeadroomAuto(bool automatic)
{
	currentState.headroom.mode = automatic
		? subroute::HeadroomMode::Auto
		: subroute::HeadroomMode::Manual;
	return commitMutation();
}

bool SubwooferRoutingUiState::setManualTrimDb(double trimDb)
{
	currentState.headroom.manualTrimDb = trimDb;
	return commitMutation();
}

bool SubwooferRoutingUiState::applyBassSendAssignments(
	const std::vector<Assignment>& assignments)
{
	SubwooferRoutingRoutingAdapter::applyBassSendAssignments(
		currentState, assignments);
	return commitMutation();
}

bool SubwooferRoutingUiState::applyOutputAssignments(
	const std::vector<Assignment>& assignments)
{
	SubwooferRoutingRoutingAdapter::applyOutputAssignments(
		currentState, assignments);
	return commitMutation();
}

bool SubwooferRoutingUiState::replaceState(
	const subroute::SubwooferRoutingState& state)
{
	currentState = state;
	return commitMutation();
}

bool SubwooferRoutingUiState::commitMutation()
{
	dirty = true;
	refreshValidation();
	return true;
}

void SubwooferRoutingUiState::refreshValidation()
{
	appliedTrimDb.reset();

	if (deviceSampleRate == 0)
	{
		currentValidation = subroute::validate(currentState);
		return;
	}

	const subroute::CompileResult compiled = subroute::compile(
		currentState,
		prepareSpecFor(currentState, deviceSampleRate));

	currentValidation = compiled.validation;
	if (compiled.headroom.has_value())
		appliedTrimDb = compiled.headroom->appliedTrimDb;
}
