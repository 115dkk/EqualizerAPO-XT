/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "SubwooferRoutingFilter.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "SubwooferRouting/Compiler.h"
#include "engine/LatencyMath.h"
#include "services/logging/Logging.h"
#include "diagnostics/performance/PerfProfile.h"
#include "SubwooferRoutingCommand.h"

namespace
{
void logValidationError(const subroute::ValidationDiagnostic& diagnostic)
{
	LogFStatic(L"SubwooferRouting: %s",
		subwooferRoutingFromUtf8(diagnostic.message).c_str());
}
}

SubwooferRoutingFilter::SubwooferRoutingFilter(
	subroute::SubwooferRoutingState state)
	: state(std::move(state))
{
}

std::vector<std::string> SubwooferRoutingFilter::deviceChannelIds(
	const subroute::SubwooferRoutingState& state,
	const std::vector<std::wstring>& channelNames)
{
	const auto stateHas = [&state](const std::string& id)
	{
		return std::any_of(state.layout.channels.begin(), state.layout.channels.end(),
			[&id](const subroute::PhysicalChannel& channel) { return channel.id == id; });
	};

	std::vector<std::string> ids;
	ids.reserve(channelNames.size());
	for (size_t index = 0; index < channelNames.size(); ++index)
	{
		std::string id = subwooferRoutingToUtf8(channelNames[index]);
		const std::string number = std::to_string(index + 1);
		if (!stateHas(id) && stateHas(number))
			id = number;
		ids.push_back(std::move(id));
	}
	return ids;
}

std::vector<std::wstring> SubwooferRoutingFilter::initialize(float sampleRate,
	unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	passthrough = true;
	latencyPaths.clear();
	latencyOutputs.clear();
	channelCount = static_cast<unsigned>(channelNames.size());

	const subroute::PrepareSpec spec {
		sampleRate,
		maxFrameCount,
		deviceChannelIds(state, channelNames)
	};

	if (maxFrameCount == 0)
	{
		LogF(L"SubwooferRouting: maximum frame count is zero; using passthrough");
		return channelNames;
	}

	if (channelNames.empty())
	{
		LogF(L"SubwooferRouting: no channels are available; using passthrough");
		return channelNames;
	}

	const subroute::ValidationResult validation =
		subroute::validate(state, spec);
	const subroute::ValidationDiagnostic* validationError =
		firstSubwooferRoutingValidationError(validation);
	if (validationError != nullptr)
	{
		logValidationError(*validationError);
		return channelNames;
	}

	subroute::CompileResult compiled = subroute::compile(state, spec);
	if (!compiled.graph.has_value())
	{
		const subroute::ValidationDiagnostic* compileError =
			firstSubwooferRoutingValidationError(compiled.validation);
		if (compileError != nullptr)
			logValidationError(*compileError);
		else
			LogF(L"SubwooferRouting: compilation produced no graph; using passthrough");
		return channelNames;
	}

	const subroute::ProcessingGraph& graph = *compiled.graph;
	try
	{
		processor.prepare(graph);
		latencyPaths = graph.paths();
		latencyOutputs = graph.outputs();
	}
	catch (const std::exception& exception)
	{
		latencyPaths.clear();
		latencyOutputs.clear();
		LogF(L"SubwooferRouting: processor preparation failed: %s",
			subwooferRoutingFromUtf8(exception.what()).c_str());
		return channelNames;
	}

	passthrough = false;
	TraceF(L"SubwooferRouting: %zu paths, %zu outputs, %.2f dB trim",
		graph.paths().size(), graph.outputs().size(),
		graph.headroom().appliedTrimDb);
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void SubwooferRoutingFilter::process(double** output, double** input,
	unsigned frameCount)
{
	PerfScope _ps("SubwooferRoutingFilter::process");
	if (passthrough)
	{
		for (unsigned channel = 0; channel < channelCount; ++channel)
		{
			if (output[channel] != input[channel])
			{
				std::copy_n(input[channel], frameCount, output[channel]);
			}
		}
		return;
	}

	subroute::AudioBlock block(
		const_cast<const double* const*>(input), output,
		channelCount, frameCount);
	processor.process(block);
}
#pragma AVRT_CODE_END

void SubwooferRoutingFilter::reportLatency(const std::vector<unsigned>& input,
	std::vector<unsigned>& output) const
{
	if (passthrough)
		return;

	std::vector<unsigned> pathLatency(latencyPaths.size(), 0);
	for (size_t pathIndex = 0; pathIndex < latencyPaths.size(); ++pathIndex)
	{
		const subroute::CompiledPath& path = latencyPaths[pathIndex];
		unsigned largestInput = 0;
		for (const subroute::CompiledSourceTerm& source : path.sourceMix)
		{
			if (source.gainLinear != 0.0 && source.inputChannelIndex < input.size())
				largestInput = (std::max)(largestInput, input[source.inputChannelIndex]);
		}

		unsigned fixedDelay = 0;
		for (const subroute::CompiledStage& stage : path.stages)
		{
			if (stage.kind != subroute::CompiledStageKind::Delay)
				continue;
			const size_t maximum = (std::numeric_limits<unsigned>::max)();
			const unsigned integerDelay = stage.integerDelaySamples > maximum
				? (std::numeric_limits<unsigned>::max)()
				: static_cast<unsigned>(stage.integerDelaySamples);
			fixedDelay = addLatency(fixedDelay, integerDelay);
			if (stage.fractionalDelaySamples > 0.0)
				fixedDelay = addLatency(fixedDelay, 1);
		}
		pathLatency[pathIndex] = addLatency(largestInput, fixedDelay);
	}

	for (const subroute::CompiledOutput& compiledOutput : latencyOutputs)
	{
		if (compiledOutput.targetChannelIndex >= output.size())
			continue;
		unsigned largest = 0;
		if (compiledOutput.mode == subroute::OutputMode::Add
			&& compiledOutput.targetChannelIndex < input.size())
		{
			largest = input[compiledOutput.targetChannelIndex];
		}
		for (const subroute::CompiledOutputTerm& term : compiledOutput.terms)
		{
			if (term.gainLinear != 0.0 && term.sourcePathIndex < pathLatency.size())
				largest = (std::max)(largest, pathLatency[term.sourcePathIndex]);
		}
		output[compiledOutput.targetChannelIndex] = largest;
	}
}
