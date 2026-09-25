/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The reading rules that pair with SubwooferRoutingUiState's setters: what
	the dialog, the card and the routing adapter show for a state. They used
	to live in three anonymous namespaces, where the dialog and the card read
	"the same" value by different rules (audit #348 B2/TD-36). Widget-free, so
	EditorLogicTests can pin each setter against its reader.
*/

#pragma once

#include <optional>
#include <string>

#include "SubwooferRouting/Crossover.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/State.h"

namespace subwooferroutingeditor
{
const subroute::Path* findPath(
	const subroute::SubwooferRoutingState& state,
	const std::string& id);
subroute::Path* findPath(
	subroute::SubwooferRoutingState& state,
	const std::string& id);

// The first source-LFE path, or nullptr.
const subroute::Path* sourceLfePath(
	const subroute::SubwooferRoutingState& state);

// The first biquad section of the type in the path's chain, or nullptr.
const subroute::BiquadFilter* firstBiquad(
	const subroute::Path& path,
	subroute::BiquadType type);

// A speaker group's crossover is read from its main paths in order: the
// corner from the first one that has a high-pass section, the slope recipe
// and the delay from the first one that resolves. The dialog's group row and
// the card's crossover summary both use these (maintainer decision, audit
// #348).
std::optional<double> groupHighPass(
	const subroute::SubwooferRoutingState& state,
	const subroute::SpeakerGroup& group);
std::optional<subroute::CrossoverRecipe> groupRecipe(
	const subroute::SubwooferRoutingState& state,
	const subroute::SpeakerGroup& group);
std::optional<double> groupDelayMs(
	const subroute::SubwooferRoutingState& state,
	const subroute::SpeakerGroup& group);

// The corner of the path's first low-pass section.
std::optional<double> pathLowPass(const subroute::Path& path);
// The path's polarity stage; false when it has none.
bool pathPolarity(const subroute::Path& path);
// The path's delay stage; 0 when it has none.
double pathDelayMs(const subroute::Path& path);

// Two different values with two names (maintainer decision, audit #348). The
// effective gain is what the source LFE actually receives: pre + post gain,
// the first source-mix term and every gain stage in the chain; the card
// shows it. The adjustment is the one editable term, preGainDb, which the
// dialog's spin box edits (SubwooferRoutingUiState::setSourceLfeGainDb).
double sourceLfeEffectiveGainDb(const subroute::Path& path);
double sourceLfeAdjustmentDb(const subroute::Path& path);

// The one built-in preset whose display name is written out rather than
// taken from its descriptor. Each widget keeps its own tr() text.
bool isIssue246Preset(const subroute::PresetDescriptor& preset);
}
