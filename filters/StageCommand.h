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

// Single owner of the "Stage:" config-line grammar, shared by the engine
// factory and the Editor GUI. Stage selectors are case-insensitive names
// separated by spaces.
struct StageCommand
{
	// The stage vocabulary. These are the only selectors the engine matches;
	// the factory reports anything else on a Stage line as unknown.
	static constexpr const wchar_t* preMix = L"pre-mix";
	static constexpr const wchar_t* postMix = L"post-mix";
	static constexpr const wchar_t* capture = L"capture";

	// Lower-cased selector tokens in the order they were written. May be empty:
	// "Stage:" with no selectors is a valid line that matches no stage.
	std::vector<std::wstring> stages;

	bool contains(const std::wstring& stage) const;

	// The engine's Stage: rule, as pure functions of the APO instance's stage
	// facts (FilterEngine::isPreMix/isCapture/isPostMixInstalled). The engine
	// factory and the Editor's channel flow both decide through these.
	//
	// Whether lines before any Stage: line run: always on the capture and
	// post-mix instances, and on the pre-mix instance only while no post-mix
	// instance is installed.
	static bool matchesByDefault(bool instancePreMix, bool instanceCapture, bool postMixInstalled);
	// Whether this Stage: line lets the lines after it run on that instance.
	// matchingStage (optional) receives the last selector that matched.
	bool matches(bool instancePreMix, bool instanceCapture, std::wstring* matchingStage = nullptr) const;
	// True for the three selectors of the stage vocabulary.
	static bool isKnownStage(const std::wstring& stage);

	// Canonical space-separated parameter string.
	std::wstring serialize() const;

	// Returns true when command names a Stage line; stages is then filled from
	// parameters (trimmed, lower-cased, split on single spaces). Unknown-token
	// policy stays with the caller.
	static bool parse(const std::wstring& command, const std::wstring& parameters, StageCommand& out);
};
