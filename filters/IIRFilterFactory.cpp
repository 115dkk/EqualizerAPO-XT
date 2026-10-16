/*
This file is part of EqualizerAPO, a system-wide equalizer.
Copyright (C) 2014  Jonas Thedering

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
#include <sstream>

#include "runtime/memory/AlignedMemory.h"
#include "services/logging/Logging.h"
#include "IIRFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "IIRFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::IIR, IIRFilterFactory, L"Filter")

using std::wstringstream;
using std::wstring;

IIRFilterFactory::IIRFilterFactory()
{
}

bool IIRFilterFactory::parseCommand(const wstring& command,
	const wstring& parameters, IIRCommand& out)
{
	return IIRCommand::parse(command, parameters, out);
}

LineOutcome IIRFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	IIRCommand cmd;
	wstring error;
	if (!IIRCommand::parse(command, parameters, cmd, error))
	{
		if (!error.empty())
			return LineOutcome::malformed(std::move(error));
		return LineOutcome::notMine();
	}

	wstringstream stream;
	stream << L"Adding IIR filter of order " << cmd.order << " with coefficients";
	for (unsigned i = 0; i <= cmd.order; i++)
		stream << L" b" << i << L"=" << cmd.coefficients[i];
	for (unsigned i = 0; i <= cmd.order; i++)
		stream << L" a" << i << L"=" << cmd.coefficients[i + cmd.order + 1];

	TraceF(L"%s", stream.str().c_str());

	return LineOutcome::produced(singleFilter(makeFilter<IIRFilter>(cmd.coefficients)));
}
