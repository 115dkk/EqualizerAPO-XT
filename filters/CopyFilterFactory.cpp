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
#include "runtime/memory/AlignedMemory.h"
#include "services/logging/Logging.h"
#include "CopyFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "CopyFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Copy, CopyFilterFactory, L"Copy")

using std::find;
using std::vector;
using std::wstring;

FilterVector CopyFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command == L"Copy")
	{
		// Parse the routing via the shared parser (parseCopyAssignments), the
		// same routine the Editor GUI factory uses.
		vector<Assignment> assignments = parseCopyAssignments(parameters);

		if (assignments.empty())
			return reportParseError(command, L"expected at least one assignment, as in \"L=R\" or \"L=0.5*L+0.5*R\"");

		return singleFilter(makeFilter<CopyFilter>(assignments));
	}

	return {};
}
