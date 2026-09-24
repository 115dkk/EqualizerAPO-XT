/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The routing a new SubwooferRouting line starts with (audit #348 TD-30):
	it ends up in the user's configuration, and until it moved out of the
	card editor's widget file only the pixel gates looked at it.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/subwooferrouting/SubwooferRoutingDefaults.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingUiState.h"

#include <QString>
#include <QStringList>

#include <string>
#include <variant>
#include <vector>

namespace
{
QString joined(const std::vector<std::string>& ids)
{
	QStringList parts;
	for (const std::string& id : ids)
		parts.append(QString::fromStdString(id));
	return parts.join(QLatin1Char(' '));
}

QString layoutOf(const subroute::SubwooferRoutingState& state)
{
	std::vector<std::string> ids;
	for (const subroute::PhysicalChannel& channel : state.layout.channels)
		ids.push_back(channel.id);
	return joined(ids);
}

QString pathsOf(const subroute::SubwooferRoutingState& state)
{
	std::vector<std::string> ids;
	for (const subroute::Path& path : state.paths)
		ids.push_back(path.id);
	return joined(ids);
}

QString outputsOf(const subroute::SubwooferRoutingState& state)
{
	std::vector<std::string> entries;
	for (const subroute::OutputMatrixEntry& entry : state.outputMatrix)
	{
		std::string text = entry.targetChannelId + "<";
		for (const subroute::OutputMatrixTerm& term : entry.terms)
			text += (text.back() == '<' ? "" : "+") + term.sourcePathId;
		entries.push_back(text);
	}
	return joined(entries);
}

const subroute::Path* findPath(const subroute::SubwooferRoutingState& state, const char* id)
{
	for (const subroute::Path& path : state.paths)
	{
		if (path.id == id)
			return &path;
	}
	return nullptr;
}

// The crossover on a path, as "HP 80 0.707" or "none".
QString crossoverOf(const subroute::Path* path)
{
	if (path == nullptr)
		return QStringLiteral("missing");
	for (const subroute::PathStage& stage : path->chain)
	{
		if (const auto* biquad = std::get_if<subroute::BiquadStage>(&stage))
		{
			const QString type = biquad->filter.type == subroute::BiquadType::HighPass ? QStringLiteral("HP")
				: biquad->filter.type == subroute::BiquadType::LowPass ? QStringLiteral("LP") : QStringLiteral("other");
			return QStringLiteral("%1 %2 %3").arg(type).arg(biquad->filter.frequencyHz)
				.arg(biquad->filter.q, 0, 'f', 3);
		}
	}
	return QStringLiteral("none");
}

QString sourcesOf(const subroute::Path* path)
{
	if (path == nullptr)
		return QStringLiteral("missing");
	std::vector<std::string> ids;
	for (const subroute::SourceMixTerm& term : path->sourceMix)
		ids.push_back(term.inputChannelId);
	return joined(ids);
}

void expectValid(const subroute::SubwooferRoutingState& state, const QString& label)
{
	expectTrue(SubwooferRoutingUiState(state, 48000).validation().succeeded(), label + QStringLiteral(": validates"));
}
}

void testSubwooferRoutingDefaultStates()
{
	using subwooferroutingeditor::buildDefaultState;

	// Stereo: the pair as two full-range main paths.
	const subroute::SubwooferRoutingState stereo = buildDefaultState({L"L", L"R"});
	expectEqual(layoutOf(stereo), QStringLiteral("L R"), "stereo layout");
	expectEqual(pathsOf(stereo), QStringLiteral("FrontLeft FrontRight"), "stereo paths");
	expectEqual(crossoverOf(findPath(stereo, "FrontLeft")), QStringLiteral("none"), "stereo mains run full range");
	expectEqual(outputsOf(stereo), QStringLiteral("L<FrontLeft R<FrontRight"), "stereo outputs");
	expectValid(stereo, QStringLiteral("stereo"));

	// 2.1: an 80 Hz Butterworth crossover, the pair summed into the LFE
	// channel beside the source LFE.
	const subroute::SubwooferRoutingState twoOne = buildDefaultState({L"L", L"R", L"LFE"});
	expectEqual(pathsOf(twoOne), QStringLiteral("FrontLeft FrontRight FrontBass SourceLFE"), "2.1 paths");
	expectEqual(crossoverOf(findPath(twoOne, "FrontRight")), QStringLiteral("HP 80 0.707"), "2.1 mains are high-passed");
	expectEqual(crossoverOf(findPath(twoOne, "FrontBass")), QStringLiteral("LP 80 0.707"), "2.1 bass is low-passed");
	expectEqual(sourcesOf(findPath(twoOne, "FrontBass")), QStringLiteral("L R"), "2.1 bass sums the pair");
	expectEqual(sourcesOf(findPath(twoOne, "SourceLFE")), QStringLiteral("LFE"), "2.1 keeps the source LFE");
	expectEqual(outputsOf(twoOne), QStringLiteral("L<FrontLeft R<FrontRight LFE<FrontBass+SourceLFE"), "2.1 outputs");
	expectValid(twoOne, QStringLiteral("2.1"));

	// 5.1: every device channel is in the layout, the routing covers the
	// front pair and the LFE, and the pair is found by name, not position.
	const subroute::SubwooferRoutingState fiveOne = buildDefaultState({L"C", L"L", L"R", L"LFE", L"SL", L"SR"});
	expectEqual(layoutOf(fiveOne), QStringLiteral("C L R LFE SL SR"), "5.1 layout keeps every channel");
	expectEqual(sourcesOf(findPath(fiveOne, "FrontLeft")), QStringLiteral("L"), "5.1 left main is L, not the first channel");
	expectEqual(outputsOf(fiveOne), QStringLiteral("L<FrontLeft R<FrontRight LFE<FrontBass+SourceLFE"), "5.1 outputs");
	expectValid(fiveOne, QStringLiteral("5.1"));

	// An LFE alone leaves no pair to route: plain L/R.
	const subroute::SubwooferRoutingState lfeOnly = buildDefaultState({L"LFE"});
	expectEqual(layoutOf(lfeOnly), QStringLiteral("L R"), "LFE-only falls back to L/R");
	expectEqual(pathsOf(lfeOnly), QStringLiteral("FrontLeft FrontRight"), "LFE-only has no bass path");

	// Repeats and names that are not stable ids are dropped; the letter case
	// of L, R and LFE does not matter.
	const subroute::SubwooferRoutingState messy = buildDefaultState({L"l", L"l", L"", L"r", L"lfe"});
	expectEqual(layoutOf(messy), QStringLiteral("l r lfe"), "repeats and empty names are dropped");
	expectEqual(outputsOf(messy), QStringLiteral("l<FrontLeft r<FrontRight lfe<FrontBass+SourceLFE"), "lower-case names route like upper-case ones");
	expectTrue(subwooferroutingeditor::isLfeChannelId("Lfe"), "LFE is recognized in any letter case");
	expectFalse(subwooferroutingeditor::isLfeChannelId("LF"), "LF is not LFE");
}
