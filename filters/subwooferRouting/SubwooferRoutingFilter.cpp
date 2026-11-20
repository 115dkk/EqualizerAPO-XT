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
#include <string>
#include <utility>
#include <vector>

#include "SubwooferRouting/Compiler.h"
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
	}
	catch (const std::exception& exception)
	{
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
