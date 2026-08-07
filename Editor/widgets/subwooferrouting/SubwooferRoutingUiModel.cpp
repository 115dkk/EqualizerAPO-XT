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

#include "SubwooferRoutingUiModel.h"

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

SubwooferRoutingUiModel::SubwooferRoutingUiModel(
	const subroute::SubwooferRoutingState& state,
	unsigned sampleRate,
	QObject* parent)
	: QObject(parent),
	  currentState(state),
	  deviceSampleRate(sampleRate)
{
	refreshValidation();
}

const subroute::SubwooferRoutingState&
SubwooferRoutingUiModel::state() const
{
	return currentState;
}

const subroute::ValidationResult&
SubwooferRoutingUiModel::validation() const
{
	return currentValidation;
}

unsigned SubwooferRoutingUiModel::sampleRate() const
{
	return deviceSampleRate;
}

bool SubwooferRoutingUiModel::isDirty() const
{
	return dirty;
}

std::optional<double> SubwooferRoutingUiModel::computedTrimDb() const
{
	return appliedTrimDb;
}

void SubwooferRoutingUiModel::setSourceLfeGainDb(double gainDb)
{
	bool changed = false;

	for (subroute::Path& path : currentState.paths)
	{
		if (path.kind != subroute::PathKind::SourceLfe)
			continue;

		path.preGainDb = gainDb;
		changed = true;
	}

	if (changed)
		commitMutation();
}

void SubwooferRoutingUiModel::setSourceLfePolarity(bool inverted)
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

	if (changed)
		commitMutation();
}

void SubwooferRoutingUiModel::setSourceLfeDelayMs(double milliseconds)
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

	if (changed)
		commitMutation();
}

void SubwooferRoutingUiModel::setGroupHighPass(
	const std::string& groupId,
	double frequencyHz)
{
	const subroute::SpeakerGroup* group =
		findGroup(currentState, groupId);
	if (group == nullptr)
		return;

	bool changed = false;
	for (const std::string& pathId : group->mainPathIds)
	{
		subroute::Path* path = findPath(currentState, pathId);
		if (path == nullptr)
			continue;

		changed |= setSectionFrequencies(*path,
			subroute::BiquadType::HighPass, frequencyHz);
	}

	if (changed)
		commitMutation();
}

void SubwooferRoutingUiModel::setBassPathLowPass(
	const std::string& pathId,
	double frequencyHz)
{
	subroute::Path* path = findPath(currentState, pathId);
	if (path == nullptr || path->kind != subroute::PathKind::Bass)
		return;

	if (setSectionFrequencies(*path,
		subroute::BiquadType::LowPass, frequencyHz))
	{
		commitMutation();
	}
}

void SubwooferRoutingUiModel::setGroupCrossover(
	const std::string& groupId,
	const subroute::CrossoverRecipe& recipe)
{
	const subroute::SpeakerGroup* group =
		findGroup(currentState, groupId);
	if (group == nullptr)
		return;

	bool changed = false;
	for (const std::string& pathId : group->mainPathIds)
	{
		subroute::Path* path = findPath(currentState, pathId);
		if (path == nullptr)
			continue;

		changed |= subroute::applyCrossoverRecipe(*path,
			subroute::BiquadType::HighPass, recipe);
	}

	if (changed)
		commitMutation();
}

void SubwooferRoutingUiModel::setBassPathCrossover(
	const std::string& pathId,
	const subroute::CrossoverRecipe& recipe)
{
	subroute::Path* path = findPath(currentState, pathId);
	if (path == nullptr || path->kind != subroute::PathKind::Bass)
		return;

	if (subroute::applyCrossoverRecipe(*path,
		subroute::BiquadType::LowPass, recipe))
	{
		commitMutation();
	}
}

void SubwooferRoutingUiModel::setGroupDelayMs(
	const std::string& groupId, double milliseconds)
{
	const subroute::SpeakerGroup* group =
		findGroup(currentState, groupId);
	if (group == nullptr)
		return;

	bool changed = false;
	for (const std::string& pathId : group->mainPathIds)
	{
		subroute::Path* path = findPath(currentState, pathId);
		if (path == nullptr)
			continue;

		changed |= setPathDelay(*path, milliseconds);
	}

	if (changed)
		commitMutation();
}

void SubwooferRoutingUiModel::setPathDelayMs(
	const std::string& pathId, double milliseconds)
{
	subroute::Path* path = findPath(currentState, pathId);
	if (path == nullptr)
		return;

	if (setPathDelay(*path, milliseconds))
		commitMutation();
}

void SubwooferRoutingUiModel::setPathPolarity(
	const std::string& pathId, bool inverted)
{
	subroute::Path* path = findPath(currentState, pathId);
	if (path == nullptr)
		return;

	for (subroute::PathStage& stage : path->chain)
	{
		subroute::PolarityStage* polarity =
			std::get_if<subroute::PolarityStage>(&stage);
		if (polarity == nullptr)
			continue;

		polarity->inverted = inverted;
		commitMutation();
		return;
	}
}

void SubwooferRoutingUiModel::setHeadroomAuto(bool automatic)
{
	currentState.headroom.mode = automatic
		? subroute::HeadroomMode::Auto
		: subroute::HeadroomMode::Manual;
	commitMutation();
}

void SubwooferRoutingUiModel::setManualTrimDb(double trimDb)
{
	currentState.headroom.manualTrimDb = trimDb;
	commitMutation();
}

void SubwooferRoutingUiModel::applyBassSendAssignments(
	const std::vector<Assignment>& assignments)
{
	SubwooferRoutingRoutingAdapter::applyBassSendAssignments(
		currentState, assignments);
	commitMutation();
}

void SubwooferRoutingUiModel::applyOutputAssignments(
	const std::vector<Assignment>& assignments)
{
	SubwooferRoutingRoutingAdapter::applyOutputAssignments(
		currentState, assignments);
	commitMutation();
}

void SubwooferRoutingUiModel::replaceState(
	const subroute::SubwooferRoutingState& state)
{
	currentState = state;
	commitMutation();
}

void SubwooferRoutingUiModel::commitMutation()
{
	dirty = true;
	refreshValidation();
	emit stateEdited();
	emit validationChanged();
}

void SubwooferRoutingUiModel::refreshValidation()
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
