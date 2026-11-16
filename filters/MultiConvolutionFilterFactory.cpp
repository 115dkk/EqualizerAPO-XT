/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"

#include "runtime/memory/AlignedMemory.h"
#include "MultiConvolutionCommand.h"
#include "ConfigFileReference.h"
#include "MultiConvolutionFilter.h"
#include "IrCache.h"
#include "filters/FilterFactoryRegistry.h"
#include "MultiConvolutionFilterFactory.h"

// Shares the Convolution priority: it runs in the same processing-filter stage
// and is told apart from "Convolution" by its command keyword.
REGISTER_FILTER_FACTORY(FilterFactoryPriority::Convolution, MultiConvolutionFilterFactory, L"MultiConvolution")

using std::vector;
using std::wstring;

LineOutcome MultiConvolutionFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	MultiConvolutionCommand cmd;
	if (!MultiConvolutionCommand::parse(command, parameters, cmd))
	{
		if (command != L"MultiConvolution")
			return LineOutcome::notMine();
		return LineOutcome::malformed(L"expected channel mappings followed by the path of an impulse response file");
	}

	ConfigFileReference::Target file = ConfigFileReference::target(configPath, cmd.path);
	if (!file)
		return LineOutcome::malformed(file.problem(L"impulse response file"));
	const wstring sizeProblem = validateIrSize(file.path);
	if (!sizeProblem.empty())
		return LineOutcome::malformed(sizeProblem);

	return LineOutcome::produced(singleFilter(makeFilter<MultiConvolutionFilter>(cmd.mappings, std::move(file.path))));
}
