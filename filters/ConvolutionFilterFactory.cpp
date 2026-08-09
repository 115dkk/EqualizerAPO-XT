/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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
#include "services/logging/Logging.h"
#include "ConvolutionCommand.h"
#include "ConvolutionFilePath.h"
#include "ConvolutionFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "ConvolutionFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Convolution, ConvolutionFilterFactory, L"Convolution")

using std::vector;
using std::wstring;

FilterVector ConvolutionFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	ConvolutionCommand cmd;
	// parse() answers false only for another command's line, so everything below
	// is this factory's own line and its failures are this factory's to report.
	if (!ConvolutionCommand::parse(command, parameters, cmd))
		return {};

	if (cmd.path.empty())
		return reportParseError(command, L"expected the path of an impulse response file");

	wstring absolutePath = ConvolutionFilePath::resolve(configPath, cmd.path);
	if (absolutePath.empty())
		return reportParseError(command, L"the impulse response file \"" + cmd.path + L"\" was not found");

	return singleFilter(makeFilter<ConvolutionFilter>(absolutePath));
}
