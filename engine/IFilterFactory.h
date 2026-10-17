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
#include <utility>
#include <vector>

#include "IFilter.h"

class FilterEngine;

struct LineOutcome
{
	enum class Kind
	{
		NotMine,
		Consumed,
		Malformed,
		Produced
	};

	Kind kind = Kind::NotMine;
	std::wstring reason;
	FilterVector filters;

	static LineOutcome notMine()
	{
		return {};
	}

	static LineOutcome consumed()
	{
		LineOutcome outcome;
		outcome.kind = Kind::Consumed;
		return outcome;
	}

	static LineOutcome malformed(std::wstring reason)
	{
		LineOutcome outcome;
		outcome.kind = Kind::Malformed;
		outcome.reason = std::move(reason);
		return outcome;
	}

	static LineOutcome produced(FilterVector filters)
	{
		LineOutcome outcome;
		outcome.kind = Kind::Produced;
		outcome.filters = std::move(filters);
		return outcome;
	}
};

#pragma AVRT_VTABLES_BEGIN
class IFilterFactory
{
public:
	virtual ~IFilterFactory() {}

	void initialize(FilterEngine& engine);
	void initialize(FilterEngine* engine);
	virtual FilterVector startOfConfiguration() {return {};}
	virtual FilterVector startOfFile(const std::wstring& configPath) {return {};}
	// The engine offers each config line to every factory in priority order.
	// Consumed and Produced stop dispatch, Malformed stops dispatch and gives the
	// engine one reason to report, and NotMine advances to the next factory.
	// `parameters` may be normalized in place for a later factory.
	virtual LineOutcome createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) = 0;
	virtual FilterVector endOfFile(const std::wstring& configPath) {return {};}
	virtual FilterVector endOfConfiguration() {return {};}

protected:
	virtual void onInitialize(FilterEngine&) {}
	FilterEngine* engine() const {return initializedEngine;}

private:
	FilterEngine* initializedEngine = nullptr;
};
#pragma AVRT_VTABLES_END
