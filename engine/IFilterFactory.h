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

#pragma once

#include <string>
#include <vector>

#include "runtime/memory/AlignedMemory.h"
#include "IFilter.h"

class FilterEngine;

#pragma AVRT_VTABLES_BEGIN
class IFilterFactory
{
public:
	virtual ~IFilterFactory() {}

	virtual void initialize(FilterEngine* engine) {}
	virtual FilterVector startOfConfiguration() {return {};}
	virtual FilterVector startOfFile(const std::wstring& configPath) {return {};}
	// Contract (see the dispatch loop in engine/FilterEngine.Configuration.cpp):
	// the engine offers each config line to every factory in priority order.
	// - Returning one or more filters consumes the line; iteration stops.
	// - Clearing `command` to L"" also consumes the line without producing a
	//   filter. Only control-flow factories (Device/If/Else/EndIf/Eval/Include/
	//   Stage) use this signal; processing factories leave `command` untouched.
	// - Leaving `command` set and returning no filters passes the line on to
	//   the next factory.
	// `parameters` may be normalized in place (e.g. decimal-comma fixes); the
	// engine does not read it back after the call.
	virtual FilterVector createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) = 0;
	virtual FilterVector endOfFile(const std::wstring& configPath) {return {};}
	virtual FilterVector endOfConfiguration() {return {};}
};
#pragma AVRT_VTABLES_END

// Base for the factories that can tell a malformed line from one that is not
// theirs.
//
// The engine cannot make that distinction: from outside a factory, "recognised
// the command and produced no filter" covers both a broken parameter list and a
// command that legitimately produces nothing (Preamp 0 dB, a disabled Filter, and
// every control-flow command). It used to guess, with a hand-maintained list of
// the commands to excuse, so the diagnosis lived in the one place that cannot
// know why a parse failed and a new no-op filter became a false warning until
// somebody remembered the list.
//
// A factory that inherits this says it itself, at the point where it knows:
//
//     if (!ConvolutionCommand::parse(command, parameters, cmd))
//         return reportParseError(command, L"expected a file path");
//
// reportParseError returns an empty FilterVector, so the failure path stays one
// line, and the report reaches both the log and the Editor's per-line trace.
class ParseReportingFactory : public IFilterFactory
{
public:
	// Factories with their own initialize() must call this one from it.
	void initialize(FilterEngine* engine) override;

protected:
	// Always returns {}. Does nothing when no engine has been handed over, which
	// is the case in the unit tests that construct a factory directly.
	FilterVector reportParseError(const std::wstring& command, const std::wstring& reason) const;

private:
	FilterEngine* reportingEngine = nullptr;
};
