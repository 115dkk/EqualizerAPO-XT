/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include <cmath>
#include <optional>
#include <string>

#include <QString>

#include "Editor/widgets/subwooferrouting/SubwooferRoutingStateReads.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingUiState.h"
#include "SubwooferRouting/Compiler.h"
#include "SubwooferRouting/Preset.h"

using namespace subwooferroutingeditor;

namespace
{
subroute::SubwooferRoutingState presetState()
{
	const subroute::PresetCreateResult preset =
		subroute::createBuiltInPreset(subroute::kIssue246FrontRear41PresetId);
	requireTrue(preset.succeeded(), "subwoofer-routing reads fixture created");
	return *preset.state;
}

const subroute::SpeakerGroup& firstGroup(const SubwooferRoutingUiState& state)
{
	requireTrue(!state.state().speakerGroups.empty(),
		"the fixture has a speaker group");
	return state.state().speakerGroups.front();
}

std::string firstBassPathId(const SubwooferRoutingUiState& state)
{
	for (const subroute::Path& path : state.state().paths)
	{
		if (path.kind == subroute::PathKind::Bass)
			return path.id;
	}
	requireTrue(false, "the fixture has a bass path");
	return std::string();
}
}

// Audit #348 B2/TD-36: every setter of SubwooferRoutingUiState against the
// reader the widgets show its value through. Before, the readers lived in the
// dialog's and the card's anonymous namespaces and nothing tied them to the
// setters.
void testSubwooferRoutingReadsRoundTripEverySetter()
{
	SubwooferRoutingUiState state(presetState(), 48000);

	requireTrue(state.setSourceLfeGainDb(-4.5), "source LFE gain set");
	requireTrue(sourceLfePath(state.state()) != nullptr, "the fixture has a source LFE path");
	expectTrue(sourceLfeAdjustmentDb(*sourceLfePath(state.state())) == -4.5,
		"the LFE gain adjustment reads back the value the setter wrote");

	requireTrue(state.setSourceLfePolarity(true), "source LFE polarity set");
	expectTrue(pathPolarity(*sourceLfePath(state.state())),
		"the source LFE polarity reads back inverted");

	requireTrue(state.setSourceLfeDelayMs(3.25), "source LFE delay set");
	expectTrue(pathDelayMs(*sourceLfePath(state.state())) == 3.25,
		"the source LFE delay reads back");

	const std::string groupId = firstGroup(state).id;
	requireTrue(state.setGroupHighPass(groupId, 95.0), "group high-pass set");
	expectTrue(groupHighPass(state.state(), firstGroup(state)) == std::optional<double>(95.0),
		"the group high-pass corner reads back");

	subroute::CrossoverRecipe groupCrossover;
	groupCrossover.alignment = subroute::CrossoverAlignment::LinkwitzRiley;
	groupCrossover.order = 4;
	groupCrossover.frequencyHz = 110.0;
	requireTrue(state.setGroupCrossover(groupId, groupCrossover), "group crossover set");
	const std::optional<subroute::CrossoverRecipe> readGroupRecipe =
		groupRecipe(state.state(), firstGroup(state));
	expectTrue(readGroupRecipe.has_value()
		&& readGroupRecipe->alignment == subroute::CrossoverAlignment::LinkwitzRiley
		&& readGroupRecipe->order == 4,
		"the group crossover recipe reads back as LR4");
	expectTrue(groupHighPass(state.state(), firstGroup(state)) == std::optional<double>(110.0),
		"the group crossover moves the corner the group row shows");

	requireTrue(state.setGroupDelayMs(groupId, 1.5), "group delay set");
	expectTrue(groupDelayMs(state.state(), firstGroup(state)) == std::optional<double>(1.5),
		"the group delay reads back");

	const std::string bassId = firstBassPathId(state);
	requireTrue(state.setBassPathLowPass(bassId, 70.0), "bass low-pass set");
	requireTrue(findPath(state.state(), bassId) != nullptr, "the bass path resolves");
	expectTrue(pathLowPass(*findPath(state.state(), bassId)) == std::optional<double>(70.0),
		"the bass low-pass corner reads back");

	subroute::CrossoverRecipe bassCrossover;
	bassCrossover.alignment = subroute::CrossoverAlignment::Butterworth;
	bassCrossover.order = 2;
	bassCrossover.frequencyHz = 85.0;
	requireTrue(state.setBassPathCrossover(bassId, bassCrossover), "bass crossover set");
	const std::optional<subroute::CrossoverRecipe> readBassRecipe =
		subroute::recognizeCrossover(*findPath(state.state(), bassId),
			subroute::BiquadType::LowPass);
	expectTrue(readBassRecipe.has_value()
		&& readBassRecipe->alignment == subroute::CrossoverAlignment::Butterworth
		&& readBassRecipe->order == 2,
		"the bass crossover recipe reads back as BW2");
	expectTrue(pathLowPass(*findPath(state.state(), bassId)) == std::optional<double>(85.0),
		"the bass crossover moves the corner the bass row shows");

	requireTrue(state.setPathDelayMs(bassId, 2.0), "path delay set");
	expectTrue(pathDelayMs(*findPath(state.state(), bassId)) == 2.0,
		"the path delay reads back");

	const bool wasInverted = pathPolarity(*findPath(state.state(), bassId));
	requireTrue(state.setPathPolarity(bassId, !wasInverted), "path polarity set");
	expectTrue(pathPolarity(*findPath(state.state(), bassId)) == !wasInverted,
		"the path polarity reads back");

	subroute::SubwooferRoutingState mutableState = state.state();
	expectTrue(findPath(mutableState, bassId) != nullptr
		&& findPath(mutableState, bassId)->id == bassId,
		"the mutable findPath finds the same path");
	expectTrue(findPath(state.state(), "no-such-path") == nullptr,
		"an unknown path id resolves to nothing");
}

// Two readers for two different values (maintainer decision, audit #348):
// the card's effective gain is the sum of every gain on the path; the
// dialog's adjustment is the editable preGainDb alone.
void testSubwooferRoutingSourceLfeGainReaders()
{
	subroute::Path path;
	path.kind = subroute::PathKind::SourceLfe;
	path.preGainDb = -3.0;
	path.postGainDb = 1.5;
	subroute::SourceMixTerm term;
	term.inputChannelId = "LFE";
	term.gainLinear = 0.5;
	path.sourceMix.push_back(term);
	subroute::GainStage lift;
	lift.gainDb = 2.0;
	subroute::GainStage trim;
	trim.gainDb = -0.5;
	path.chain.push_back(lift);
	path.chain.push_back(trim);

	// -3 + 1.5 + 2 - 0.5 = 0, plus 20 log10(0.5) = -6.0205999...
	const double expected = 20.0 * std::log10(0.5);
	expectTrue(std::abs(sourceLfeEffectiveGainDb(path) - expected) < 1.0e-12,
		QStringLiteral("the effective LFE gain sums every term: expected %1, got %2")
			.arg(expected, 0, 'f', 9).arg(sourceLfeEffectiveGainDb(path), 0, 'f', 9));
	expectTrue(sourceLfeAdjustmentDb(path) == -3.0,
		"the LFE gain adjustment is preGainDb alone");
}

// One preview rule (maintainer decision, audit #348): with no device the
// preview compiles at 48 kHz, and the trim the UI state reports is that
// compile's trim.
void testSubwooferRoutingPreviewRateAndTrim()
{
	expectTrue(SubwooferRoutingUiState::previewSampleRateFor(44100) == 44100.0,
		"a device rate is the preview rate");
	expectTrue(SubwooferRoutingUiState::previewSampleRateFor(0) == 48000.0,
		"no device previews at 48 kHz");
	expectTrue(SubwooferRoutingUiState::kPreviewFallbackSampleRate
			== subroute::kPreviewFallbackSampleRate,
		"the Editor's fallback is the core's");

	const subroute::SubwooferRoutingState fixture = presetState();
	const SubwooferRoutingUiState noDevice(fixture, 0);
	expectTrue(noDevice.previewSampleRate() == 48000.0,
		"the UI state previews at 48 kHz without a device");

	const subroute::CompileResult reference = subroute::compile(
		fixture, subroute::previewSpecFor(fixture, 48000.0));
	requireTrue(reference.headroom.has_value(), "the 48 kHz reference compile has a headroom analysis");
	expectTrue(noDevice.computedTrimDb() == std::optional<double>(reference.headroom->appliedTrimDb),
		"with rate 0 the reported trim is the 48 kHz compile's trim");
	expectTrue(noDevice.computedTrimDb() == SubwooferRoutingUiState(fixture, 48000).computedTrimDb(),
		"with rate 0 the trim equals the one a 48 kHz device shows");
}
