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

#include "SubwooferRoutingRoutingAdapter.h"

#include "SubwooferRouting/Compiler.h"

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

	if (!subroute::isValidStableId(result))
		return std::nullopt;

	return result;
}

QString toQStringId(const std::string& id)
{
	return QString::fromLatin1(id.data(), static_cast<int>(id.size()));
}

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

std::vector<subroute::SourceMixTerm> groupSourceMix(
	const subroute::SubwooferRoutingState& state,
	const subroute::SpeakerGroup& group)
{
	std::vector<subroute::SourceMixTerm> result;
	std::unordered_set<std::string> seen;

	for (const std::string& pathId : group.mainPathIds)
	{
		const subroute::Path* path = findPath(state, pathId);
		if (path == nullptr || path->kind != subroute::PathKind::Main)
			continue;

		for (const subroute::SourceMixTerm& term : path->sourceMix)
		{
			if (seen.insert(term.inputChannelId).second)
				result.push_back(term);
		}
	}

	return result;
}

bool containsSourceMix(
	const std::vector<subroute::SourceMixTerm>& candidate,
	const std::vector<subroute::SourceMixTerm>& reference)
{
	if (reference.empty())
		return false;

	for (const subroute::SourceMixTerm& expected : reference)
	{
		const auto found = std::find_if(candidate.begin(), candidate.end(),
			[&expected](const subroute::SourceMixTerm& term)
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
SubwooferRoutingRoutingAdapter::toBassSendAssignments(
	const subroute::SubwooferRoutingState& state)
{
	std::vector<Assignment> result;

	for (const subroute::Path& bassPath : state.paths)
	{
		if (bassPath.kind != subroute::PathKind::Bass)
			continue;

		Assignment assignment;
		assignment.targetChannel = toWideId(bassPath.id);

		for (const subroute::SpeakerGroup& group : state.speakerGroups)
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

		for (const subroute::Path& sourceLfePath : state.paths)
		{
			if (sourceLfePath.kind != subroute::PathKind::SourceLfe)
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

QStringList SubwooferRoutingRoutingAdapter::bassSendSources(
	const subroute::SubwooferRoutingState& state)
{
	QStringList result;

	for (const subroute::SpeakerGroup& group : state.speakerGroups)
		result.append(toQStringId(group.id));

	for (const subroute::Path& path : state.paths)
	{
		if (path.kind == subroute::PathKind::SourceLfe)
			result.append(toQStringId(path.id));
	}

	return result;
}

void SubwooferRoutingRoutingAdapter::applyBassSendAssignments(
	subroute::SubwooferRoutingState& state,
	const std::vector<Assignment>& assignments)
{
	std::unordered_map<std::string, subroute::SpeakerGroup*> groups;
	for (subroute::SpeakerGroup& group : state.speakerGroups)
		groups.emplace(group.id, &group);

	std::unordered_map<std::string, const subroute::Path*> sourceLfePaths;
	std::unordered_set<std::string> bassPathIds;
	for (const subroute::Path& path : state.paths)
	{
		if (path.kind == subroute::PathKind::Bass)
			bassPathIds.insert(path.id);
		else if (path.kind == subroute::PathKind::SourceLfe)
			sourceLfePaths.emplace(path.id, &path);
	}

	std::unordered_map<std::string, const Assignment*> assignmentsByTarget;
	for (const Assignment& assignment : assignments)
	{
		const std::optional<std::string> target =
			fromWideId(assignment.targetChannel);
		if (!target.has_value()
			|| !bassPathIds.contains(*target))
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
				|| !groups.contains(*source))
			{
				continue;
			}

			groupTargets.emplace(*source, targetAssignment.first);
		}
	}

	for (subroute::SpeakerGroup& group : state.speakerGroups)
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

	for (subroute::Path& bassPath : state.paths)
	{
		if (bassPath.kind != subroute::PathKind::Bass)
			continue;

		const auto assignment = assignmentsByTarget.find(bassPath.id);
		if (assignment == assignmentsByTarget.end())
			continue;

		std::vector<subroute::SourceMixTerm> desiredTemplate;
		std::unordered_set<std::string> desiredInputs;

		for (const Assignment::Summand& summand
			: assignment->second->sourceSum)
		{
			const std::optional<std::string> source =
				fromWideId(summand.channel);
			if (!source.has_value())
				continue;

			std::vector<subroute::SourceMixTerm> sourceMix;

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

			for (const subroute::SourceMixTerm& term : sourceMix)
			{
				if (desiredInputs.insert(term.inputChannelId).second)
					desiredTemplate.push_back(term);
			}
		}

		std::vector<subroute::SourceMixTerm> replacement;
		replacement.reserve(desiredTemplate.size());

		for (const subroute::SourceMixTerm& desired : desiredTemplate)
		{
			const auto existing = std::find_if(
				bassPath.sourceMix.begin(), bassPath.sourceMix.end(),
				[&desired](const subroute::SourceMixTerm& term)
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
SubwooferRoutingRoutingAdapter::toOutputAssignments(
	const subroute::SubwooferRoutingState& state)
{
	std::vector<Assignment> result;
	result.reserve(state.outputMatrix.size());

	for (const subroute::OutputMatrixEntry& output : state.outputMatrix)
	{
		Assignment assignment;
		assignment.targetChannel = toWideId(output.targetChannelId);

		for (const subroute::OutputMatrixTerm& term : output.terms)
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

QStringList SubwooferRoutingRoutingAdapter::outputSources(
	const subroute::SubwooferRoutingState& state)
{
	QStringList result;

	for (const subroute::Path& path : state.paths)
		result.append(toQStringId(path.id));

	return result;
}

void SubwooferRoutingRoutingAdapter::applyOutputAssignments(
	subroute::SubwooferRoutingState& state,
	const std::vector<Assignment>& assignments)
{
	std::unordered_set<std::string> physicalChannels;
	for (const subroute::PhysicalChannel& channel : state.layout.channels)
		physicalChannels.insert(channel.id);

	std::unordered_set<std::string> pathIds;
	for (const subroute::Path& path : state.paths)
		pathIds.insert(path.id);

	struct EditedOutput
	{
		std::string target;
		std::vector<subroute::OutputMatrixTerm> terms;
	};

	std::vector<EditedOutput> editedOutputs;
	std::unordered_set<std::string> seenTargets;

	for (const Assignment& assignment : assignments)
	{
		const std::optional<std::string> target =
			fromWideId(assignment.targetChannel);
		if (!target.has_value()
			|| !physicalChannels.contains(*target)
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
				|| !pathIds.contains(*source))
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
	for (subroute::OutputMatrixEntry& output : state.outputMatrix)
	{
		existingTargets.insert(output.targetChannelId);

		const auto edited = editedByTarget.find(output.targetChannelId);
		if (edited != editedByTarget.end())
			output.terms = edited->second->terms;
	}

	for (const EditedOutput& edited : editedOutputs)
	{
		if (existingTargets.contains(edited.target))
			continue;

		subroute::OutputMatrixEntry output;
		output.targetChannelId = edited.target;
		output.mode = subroute::OutputMode::Replace;
		output.terms = edited.terms;
		state.outputMatrix.push_back(std::move(output));
	}
}
