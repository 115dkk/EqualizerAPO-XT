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
#include "engine/IFilterFactory.h"
#include "engine/IFilter.h"

class EngineParser;
namespace mup { class Value; }

class IfFilterFactory : public ParseReportingFactory
{
public:
	void initialize(FilterEngine* engine) override;
	FilterVector startOfConfiguration() override;
	FilterVector startOfFile(const std::wstring& configPath) override;
	FilterVector createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;
	FilterVector endOfFile(const std::wstring& configPath) override;

private:
	EngineParser* parser = nullptr;
	FilterEngine* engine = nullptr;

	unsigned trueCount = 0;
	unsigned falseCount = 0;
	bool executeElse = false;
	std::stack<unsigned> trueCountStack;
	// The lines of the Ifs in the current file that no EndIf has closed yet,
	// so an unclosed one is reported on its own line rather than on the end
	// of the file, where the Editor has no row to show it on. Saved across an
	// Include like trueCount.
	std::vector<int> openIfLines;
	std::stack<std::vector<int>> openIfLinesStack;

	bool toBoolean(const mup::Value& value);
};
