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
#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "CopyFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "CopyFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Copy, CopyFilterFactory, false, L"Copy")

using std::find;
using std::vector;
using std::wstring;

vector<IFilter*> CopyFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	CopyFilter* filter = nullptr;

	if (command == L"Copy")
	{
		// Parse the routing into the shared std::vector<Assignment> (the same type
		// CopyFilter::getAssignments() returns) via the single shared parser. This
		// reproduces the former inline grammar verbatim, so the resulting filter -
		// and therefore copy_crossfeed - stays bit-identical; the Editor GUI factory
		// now parses through the exact same routine instead of building a throwaway
		// CopyFilter just to read getAssignments() back.
		vector<Assignment> assignments = parseCopyAssignments(parameters);

		filter = MemoryHelper::construct<CopyFilter>(assignments);
	}

	if (filter == nullptr)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}
