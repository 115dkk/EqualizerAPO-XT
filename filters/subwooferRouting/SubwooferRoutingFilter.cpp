/*
	This file is part of EqualizerAPO-XT.

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
#include "services/logging/LogHelper.h"
#include "diagnostics/performance/PerfProfile.h"
#include "text/StringHelper.h"
#include "SubwooferRoutingCommand.h"

namespace
{
constexpr unsigned Utf8CodePage = 65001;

std::wstring fromUtf8(const std::string& text)
{
	return StringHelper::toWString(text, Utf8CodePage);
}

const subroute::ValidationDiagnostic* firstValidationError(
	const subroute::ValidationResult& result)
{
	for (const subroute::ValidationDiagnostic& diagnostic : result.diagnostics)
	{
		if (diagnostic.severity == subroute::DiagnosticSeverity::Error)
			return &diagnostic;
	}
	return nullptr;
}

void logValidationError(const subroute::ValidationDiagnostic& diagnostic)
{
	LogFStatic(L"SubwooferRouting: %s", fromUtf8(diagnostic.message).c_str());
}
}

SubwooferRoutingFilter::SubwooferRoutingFilter(
	subroute::SubwooferRoutingState state)
	: state(std::move(state))
{
}

std::vector<std::wstring> SubwooferRoutingFilter::initialize(float sampleRate,
	unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	passthrough = true;
	channelCount = static_cast<unsigned>(channelNames.size());

	std::vector<std::string> layout;
	layout.reserve(channelNames.size());
	for (const std::wstring& channelName : channelNames)
		layout.push_back(subwooferRoutingToUtf8(channelName));

	const subroute::PrepareSpec spec {
		sampleRate,
		maxFrameCount,
		std::move(layout)
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
		firstValidationError(validation);
	if (validationError != nullptr)
	{
		logValidationError(*validationError);
		return channelNames;
	}

	subroute::CompileResult compiled = subroute::compile(state, spec);
	if (!compiled.graph.has_value())
	{
		const subroute::ValidationDiagnostic* compileError =
			firstValidationError(compiled.validation);
		if (compileError != nullptr)
			logValidationError(*compileError);
		else
			LogF(L"SubwooferRouting: compilation produced no graph; using passthrough");
		return channelNames;
	}

	const subroute::ProcessingGraph& graph = *compiled.graph;
	try
	{
		processor.prepare(spec, graph);
	}
	catch (const std::exception& exception)
	{
		LogF(L"SubwooferRouting: processor preparation failed: %s",
			fromUtf8(exception.what()).c_str());
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
