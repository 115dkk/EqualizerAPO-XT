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

#include "BassManagementUiModel.h"

#include <algorithm>
#include <variant>

#include "Editor/widgets/routing/BassManagementRoutingAdapter.h"

namespace
{
bassmgmt::PrepareSpec prepareSpecFor(
	const bassmgmt::BassManagementState& state,
	unsigned sampleRate)
{
	bassmgmt::PrepareSpec spec;
	spec.sampleRate = sampleRate;
	spec.maximumBlockSize = 1024;
	spec.channelLayout.reserve(state.layout.channels.size());

	for (const bassmgmt::PhysicalChannel& channel
		: state.layout.channels)
	{
		spec.channelLayout.push_back(channel.id);
	}

	return spec;
}

bassmgmt::Path* findPath(
	bassmgmt::BassManagementState& state,
	const std::string& id)
{
	const auto path = std::find_if(state.paths.begin(), state.paths.end(),
		[&id](const bassmgmt::Path& candidate)
		{
			return candidate.id == id;
		});

	return path == state.paths.end() ? nullptr : &*path;
}

bassmgmt::BiquadFilter* firstBiquad(
	bassmgmt::Path& path,
	bassmgmt::BiquadType type)
{
	for (bassmgmt::PathStage& stage : path.chain)
	{
		bassmgmt::BiquadStage* biquad =
			std::get_if<bassmgmt::BiquadStage>(&stage);
		if (biquad != nullptr && biquad->filter.type == type)
			return &biquad->filter;
	}

	return nullptr;
}
}

BassManagementUiModel::BassManagementUiModel(
	const bassmgmt::BassManagementState& state,
	unsigned sampleRate,
	QObject* parent)
	: QObject(parent),
	  currentState(state),
	  deviceSampleRate(sampleRate)
{
	refreshValidation();
}

const bassmgmt::BassManagementState&
BassManagementUiModel::state() const
{
	return currentState;
}

const bassmgmt::ValidationResult&
BassManagementUiModel::validation() const
{
	return currentValidation;
}

unsigned BassManagementUiModel::sampleRate() const
{
	return deviceSampleRate;
}

bool BassManagementUiModel::isDirty() const
{
	return dirty;
}

std::optional<double> BassManagementUiModel::computedTrimDb() const
{
	return appliedTrimDb;
}

void BassManagementUiModel::setSourceLfeGainDb(double gainDb)
{
	bool changed = false;

	for (bassmgmt::Path& path : currentState.paths)
	{
		if (path.kind != bassmgmt::PathKind::SourceLfe)
			continue;

		path.preGainDb = gainDb;
		changed = true;
	}

	if (changed)
		commitMutation();
}

void BassManagementUiModel::setSourceLfePolarity(bool inverted)
{
	bool changed = false;

	for (bassmgmt::Path& path : currentState.paths)
	{
		if (path.kind != bassmgmt::PathKind::SourceLfe)
			continue;

		for (bassmgmt::PathStage& stage : path.chain)
		{
			bassmgmt::PolarityStage* polarity =
				std::get_if<bassmgmt::PolarityStage>(&stage);
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

void BassManagementUiModel::setSourceLfeDelayMs(double milliseconds)
{
	bool changed = false;

	for (bassmgmt::Path& path : currentState.paths)
	{
		if (path.kind != bassmgmt::PathKind::SourceLfe)
			continue;

		for (bassmgmt::PathStage& stage : path.chain)
		{
			bassmgmt::DelayStage* delay =
				std::get_if<bassmgmt::DelayStage>(&stage);
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

void BassManagementUiModel::setGroupHighPass(
	const std::string& groupId,
	double frequencyHz)
{
	const auto group = std::find_if(
		currentState.speakerGroups.begin(),
		currentState.speakerGroups.end(),
		[&groupId](const bassmgmt::SpeakerGroup& candidate)
		{
			return candidate.id == groupId;
		});

	if (group == currentState.speakerGroups.end())
		return;

	bool changed = false;
	for (const std::string& pathId : group->mainPathIds)
	{
		bassmgmt::Path* path = findPath(currentState, pathId);
		if (path == nullptr)
			continue;

		bassmgmt::BiquadFilter* filter =
			firstBiquad(*path, bassmgmt::BiquadType::HighPass);
		if (filter == nullptr)
			continue;

		filter->frequencyHz = frequencyHz;
		changed = true;
	}

	if (changed)
		commitMutation();
}

void BassManagementUiModel::setBassPathLowPass(
	const std::string& pathId,
	double frequencyHz)
{
	bassmgmt::Path* path = findPath(currentState, pathId);
	if (path == nullptr || path->kind != bassmgmt::PathKind::Bass)
		return;

	bassmgmt::BiquadFilter* filter =
		firstBiquad(*path, bassmgmt::BiquadType::LowPass);
	if (filter == nullptr)
		return;

	filter->frequencyHz = frequencyHz;
	commitMutation();
}

void BassManagementUiModel::setHeadroomAuto(bool automatic)
{
	currentState.headroom.mode = automatic
		? bassmgmt::HeadroomMode::Auto
		: bassmgmt::HeadroomMode::Manual;
	commitMutation();
}

void BassManagementUiModel::setManualTrimDb(double trimDb)
{
	currentState.headroom.manualTrimDb = trimDb;
	commitMutation();
}

void BassManagementUiModel::applyBassSendAssignments(
	const std::vector<Assignment>& assignments)
{
	BassManagementRoutingAdapter::applyBassSendAssignments(
		currentState, assignments);
	commitMutation();
}

void BassManagementUiModel::applyOutputAssignments(
	const std::vector<Assignment>& assignments)
{
	BassManagementRoutingAdapter::applyOutputAssignments(
		currentState, assignments);
	commitMutation();
}

void BassManagementUiModel::replaceState(
	const bassmgmt::BassManagementState& state)
{
	currentState = state;
	commitMutation();
}

void BassManagementUiModel::commitMutation()
{
	dirty = true;
	refreshValidation();
	emit stateEdited();
	emit validationChanged();
}

void BassManagementUiModel::refreshValidation()
{
	appliedTrimDb.reset();

	if (deviceSampleRate == 0)
	{
		currentValidation = bassmgmt::validate(currentState);
		return;
	}

	const bassmgmt::CompileResult compiled = bassmgmt::compile(
		currentState,
		prepareSpecFor(currentState, deviceSampleRate));

	currentValidation = compiled.validation;
	if (compiled.headroom.has_value())
		appliedTrimDb = compiled.headroom->appliedTrimDb;
}
