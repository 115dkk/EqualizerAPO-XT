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

#include "BassManagementRoutingAdapter.h"

#include "BassManagement/Compiler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
std::wstring toWideId(const std::string& id)
{
	return std::wstring(id.begin(), id.end());
}

std::optional<std::string> fromWideId(const std::wstring& id)
{
	std::string result;
	result.reserve(id.size());

	for (const wchar_t character : id)
	{
		if (character < 0 || character > 0x7f)
			return std::nullopt;

		result.push_back(static_cast<char>(character));
	}

	if (!bassmgmt::isValidStableId(result))
		return std::nullopt;

	return result;
}

QString toQStringId(const std::string& id)
{
	return QString::fromLatin1(id.data(), static_cast<int>(id.size()));
}

const bassmgmt::Path* findPath(
	const bassmgmt::BassManagementState& state,
	const std::string& id)
{
	const auto path = std::find_if(state.paths.begin(), state.paths.end(),
		[&id](const bassmgmt::Path& candidate)
		{
			return candidate.id == id;
		});

	return path == state.paths.end() ? nullptr : &*path;
}

std::vector<bassmgmt::SourceMixTerm> groupSourceMix(
	const bassmgmt::BassManagementState& state,
	const bassmgmt::SpeakerGroup& group)
{
	std::vector<bassmgmt::SourceMixTerm> result;
	std::unordered_set<std::string> seen;

	for (const std::string& pathId : group.mainPathIds)
	{
		const bassmgmt::Path* path = findPath(state, pathId);
		if (path == nullptr || path->kind != bassmgmt::PathKind::Main)
			continue;

		for (const bassmgmt::SourceMixTerm& term : path->sourceMix)
		{
			if (seen.insert(term.inputChannelId).second)
				result.push_back(term);
		}
	}

	return result;
}

bool containsSourceMix(
	const std::vector<bassmgmt::SourceMixTerm>& candidate,
	const std::vector<bassmgmt::SourceMixTerm>& reference)
{
	if (reference.empty())
		return false;

	for (const bassmgmt::SourceMixTerm& expected : reference)
	{
		const auto found = std::find_if(candidate.begin(), candidate.end(),
			[&expected](const bassmgmt::SourceMixTerm& term)
			{
				return term.inputChannelId == expected.inputChannelId;
			});

		if (found == candidate.end())
			return false;
	}

	return true;
}

std::optional<double> assignmentFactorToDb(
	const Assignment::Summand& summand)
{
	if (!std::isfinite(summand.factor))
		return std::nullopt;

	if (summand.isDecibel)
		return summand.factor;

	if (summand.factor <= 0.0)
		return std::nullopt;

	return 20.0 * std::log10(summand.factor);
}
}

std::vector<Assignment>
BassManagementRoutingAdapter::toBassSendAssignments(
	const bassmgmt::BassManagementState& state)
{
	std::vector<Assignment> result;

	for (const bassmgmt::Path& bassPath : state.paths)
	{
		if (bassPath.kind != bassmgmt::PathKind::Bass)
			continue;

		Assignment assignment;
		assignment.targetChannel = toWideId(bassPath.id);

		for (const bassmgmt::SpeakerGroup& group : state.speakerGroups)
		{
			if (!group.bassPathId.has_value()
				|| *group.bassPathId != bassPath.id)
			{
				continue;
			}

			Assignment::Summand summand;
			summand.factor = 1.0;
			summand.isDecibel = false;
			summand.channel = toWideId(group.id);
			assignment.sourceSum.push_back(std::move(summand));
		}

		for (const bassmgmt::Path& sourceLfePath : state.paths)
		{
			if (sourceLfePath.kind != bassmgmt::PathKind::SourceLfe)
				continue;

			if (!containsSourceMix(
				bassPath.sourceMix, sourceLfePath.sourceMix))
			{
				continue;
			}

			Assignment::Summand summand;
			summand.factor = 1.0;
			summand.isDecibel = false;
			summand.channel = toWideId(sourceLfePath.id);
			assignment.sourceSum.push_back(std::move(summand));
		}

		result.push_back(std::move(assignment));
	}

	return result;
}

QStringList BassManagementRoutingAdapter::bassSendSources(
	const bassmgmt::BassManagementState& state)
{
	QStringList result;

	for (const bassmgmt::SpeakerGroup& group : state.speakerGroups)
		result.append(toQStringId(group.id));

	for (const bassmgmt::Path& path : state.paths)
	{
		if (path.kind == bassmgmt::PathKind::SourceLfe)
			result.append(toQStringId(path.id));
	}

	return result;
}

void BassManagementRoutingAdapter::applyBassSendAssignments(
	bassmgmt::BassManagementState& state,
	const std::vector<Assignment>& assignments)
{
	std::unordered_map<std::string, bassmgmt::SpeakerGroup*> groups;
	for (bassmgmt::SpeakerGroup& group : state.speakerGroups)
		groups.emplace(group.id, &group);

	std::unordered_map<std::string, const bassmgmt::Path*> sourceLfePaths;
	std::unordered_set<std::string> bassPathIds;
	for (const bassmgmt::Path& path : state.paths)
	{
		if (path.kind == bassmgmt::PathKind::Bass)
			bassPathIds.insert(path.id);
		else if (path.kind == bassmgmt::PathKind::SourceLfe)
			sourceLfePaths.emplace(path.id, &path);
	}

	std::unordered_map<std::string, const Assignment*> assignmentsByTarget;
	for (const Assignment& assignment : assignments)
	{
		const std::optional<std::string> target =
			fromWideId(assignment.targetChannel);
		if (!target.has_value()
			|| bassPathIds.find(*target) == bassPathIds.end())
		{
			continue;
		}

		assignmentsByTarget.emplace(*target, &assignment);
	}

	std::unordered_map<std::string, std::string> groupTargets;
	for (const auto& targetAssignment : assignmentsByTarget)
	{
		for (const Assignment::Summand& summand
			: targetAssignment.second->sourceSum)
		{
			const std::optional<std::string> source =
				fromWideId(summand.channel);
			if (!source.has_value()
				|| groups.find(*source) == groups.end())
			{
				continue;
			}

			groupTargets.emplace(*source, targetAssignment.first);
		}
	}

	for (bassmgmt::SpeakerGroup& group : state.speakerGroups)
	{
		const auto target = groupTargets.find(group.id);
		if (target != groupTargets.end())
		{
			group.bassPathId = target->second;
			continue;
		}

		if (group.bassPathId.has_value()
			&& assignmentsByTarget.find(*group.bassPathId)
				!= assignmentsByTarget.end())
		{
			group.bassPathId.reset();
		}
	}

	for (bassmgmt::Path& bassPath : state.paths)
	{
		if (bassPath.kind != bassmgmt::PathKind::Bass)
			continue;

		const auto assignment = assignmentsByTarget.find(bassPath.id);
		if (assignment == assignmentsByTarget.end())
			continue;

		std::vector<bassmgmt::SourceMixTerm> desiredTemplate;
		std::unordered_set<std::string> desiredInputs;

		for (const Assignment::Summand& summand
			: assignment->second->sourceSum)
		{
			const std::optional<std::string> source =
				fromWideId(summand.channel);
			if (!source.has_value())
				continue;

			std::vector<bassmgmt::SourceMixTerm> sourceMix;

			const auto group = groups.find(*source);
			if (group != groups.end())
			{
				sourceMix = groupSourceMix(state, *group->second);
			}
			else
			{
				const auto sourceLfe = sourceLfePaths.find(*source);
				if (sourceLfe == sourceLfePaths.end())
					continue;

				sourceMix = sourceLfe->second->sourceMix;
			}

			for (const bassmgmt::SourceMixTerm& term : sourceMix)
			{
				if (desiredInputs.insert(term.inputChannelId).second)
					desiredTemplate.push_back(term);
			}
		}

		std::vector<bassmgmt::SourceMixTerm> replacement;
		replacement.reserve(desiredTemplate.size());

		for (const bassmgmt::SourceMixTerm& desired : desiredTemplate)
		{
			const auto existing = std::find_if(
				bassPath.sourceMix.begin(), bassPath.sourceMix.end(),
				[&desired](const bassmgmt::SourceMixTerm& term)
				{
					return term.inputChannelId
						== desired.inputChannelId;
				});

			replacement.push_back(
				existing == bassPath.sourceMix.end()
					? desired
					: *existing);
		}

		bassPath.sourceMix = std::move(replacement);
	}
}

std::vector<Assignment>
BassManagementRoutingAdapter::toOutputAssignments(
	const bassmgmt::BassManagementState& state)
{
	std::vector<Assignment> result;
	result.reserve(state.outputMatrix.size());

	for (const bassmgmt::OutputMatrixEntry& output : state.outputMatrix)
	{
		Assignment assignment;
		assignment.targetChannel = toWideId(output.targetChannelId);

		for (const bassmgmt::OutputMatrixTerm& term : output.terms)
		{
			Assignment::Summand summand;
			summand.factor = term.gainDb;
			summand.isDecibel = true;
			summand.channel = toWideId(term.sourcePathId);
			assignment.sourceSum.push_back(std::move(summand));
		}

		result.push_back(std::move(assignment));
	}

	return result;
}

QStringList BassManagementRoutingAdapter::outputSources(
	const bassmgmt::BassManagementState& state)
{
	QStringList result;

	for (const bassmgmt::Path& path : state.paths)
		result.append(toQStringId(path.id));

	return result;
}

void BassManagementRoutingAdapter::applyOutputAssignments(
	bassmgmt::BassManagementState& state,
	const std::vector<Assignment>& assignments)
{
	std::unordered_set<std::string> physicalChannels;
	for (const bassmgmt::PhysicalChannel& channel : state.layout.channels)
		physicalChannels.insert(channel.id);

	std::unordered_set<std::string> pathIds;
	for (const bassmgmt::Path& path : state.paths)
		pathIds.insert(path.id);

	struct EditedOutput
	{
		std::string target;
		std::vector<bassmgmt::OutputMatrixTerm> terms;
	};

	std::vector<EditedOutput> editedOutputs;
	std::unordered_set<std::string> seenTargets;

	for (const Assignment& assignment : assignments)
	{
		const std::optional<std::string> target =
			fromWideId(assignment.targetChannel);
		if (!target.has_value()
			|| physicalChannels.find(*target) == physicalChannels.end()
			|| !seenTargets.insert(*target).second)
		{
			continue;
		}

		EditedOutput edited;
		edited.target = *target;

		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			const std::optional<std::string> source =
				fromWideId(summand.channel);
			const std::optional<double> gainDb =
				assignmentFactorToDb(summand);

			if (!source.has_value() || !gainDb.has_value()
				|| pathIds.find(*source) == pathIds.end())
			{
				continue;
			}

			edited.terms.push_back({*source, *gainDb});
		}

		editedOutputs.push_back(std::move(edited));
	}

	std::unordered_map<std::string, const EditedOutput*> editedByTarget;
	for (const EditedOutput& edited : editedOutputs)
		editedByTarget.emplace(edited.target, &edited);

	std::unordered_set<std::string> existingTargets;
	for (bassmgmt::OutputMatrixEntry& output : state.outputMatrix)
	{
		existingTargets.insert(output.targetChannelId);

		const auto edited = editedByTarget.find(output.targetChannelId);
		if (edited != editedByTarget.end())
			output.terms = edited->second->terms;
	}

	for (const EditedOutput& edited : editedOutputs)
	{
		if (existingTargets.find(edited.target) != existingTargets.end())
			continue;

		bassmgmt::OutputMatrixEntry output;
		output.targetChannelId = edited.target;
		output.mode = bassmgmt::OutputMode::Replace;
		output.terms = edited.terms;
		state.outputMatrix.push_back(std::move(output));
	}
}
