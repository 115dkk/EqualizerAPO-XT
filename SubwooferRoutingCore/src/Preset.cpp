// SPDX-License-Identifier: MIT

#include "SubwooferRouting/Preset.h"

#include <utility>
#include <vector>

namespace subroute
{

namespace
{

// The issue #246 source configuration writes "Q 0.707" literally, and parity
// against that original filter chain requires the exact same coefficient
// inputs - so this is 0.707, not a rounded sqrt(1/2).
constexpr double kCrossoverQ = 0.707;

BiquadFilter makeCrossover(
	BiquadType type,
	double frequencyHz)
{
	BiquadFilter filter;
	filter.type = type;
	filter.frequencyHz = frequencyHz;
	filter.q = kCrossoverQ;
	filter.gainDb = 0.0;
	return filter;
}

Path makePath(
	std::string id,
	PathKind kind,
	std::vector<SourceMixTerm> sourceMix,
	double preGainDb,
	const std::vector<BiquadFilter>& crossovers,
	double delayMilliseconds)
{
	Path path;
	path.id = std::move(id);
	path.kind = kind;
	path.sourceMix = std::move(sourceMix);
	path.preGainDb = preGainDb;
	path.postGainDb = 0.0;

	path.chain.emplace_back(PolarityStage{false});

	for (const BiquadFilter& crossover : crossovers)
	{
		path.chain.emplace_back(BiquadStage{crossover});
	}

	path.chain.emplace_back(DelayStage{delayMilliseconds});
	path.chain.emplace_back(EqualizerSlotsStage{});

	return path;
}

OutputMatrixEntry makeOutput(
	std::string targetChannelId,
	std::vector<OutputMatrixTerm> terms)
{
	OutputMatrixEntry output;
	output.targetChannelId = std::move(targetChannelId);
	output.mode = OutputMode::Replace;
	output.terms = std::move(terms);
	return output;
}

SubwooferRoutingState makeIssue246FrontRear41Preset()
{
	SubwooferRoutingState state;

	state.layout.channels = {
		{"L", "Left"},
		{"R", "Right"},
		{"LFE", "Low-frequency effects"},
		{"RL", "Rear left"},
		{"RR", "Rear right"}
	};

	SpeakerGroup front;
	front.id = "Front";
	front.displayName = "Front";
	front.mainPathIds = {"FrontL", "FrontR"};
	front.bassPathId = "FrontBass";

	SpeakerGroup rear;
	rear.id = "Rear";
	rear.displayName = "Rear";
	rear.mainPathIds = {"RearL", "RearR"};
	rear.bassPathId = "RearBass";

	state.speakerGroups = {
		std::move(front),
		std::move(rear)
	};

	state.paths.push_back(makePath(
		"FrontL",
		PathKind::Main,
		{{"L", 1.0}},
		0.0,
		{makeCrossover(BiquadType::HighPass, 80.0)},
		2.5));

	state.paths.push_back(makePath(
		"FrontR",
		PathKind::Main,
		{{"R", 1.0}},
		0.0,
		{makeCrossover(BiquadType::HighPass, 80.0)},
		2.5));

	state.paths.push_back(makePath(
		"FrontBass",
		PathKind::Bass,
		{
			{"L", -1.0},
			{"R", -1.0}
		},
		0.0,
		{
			makeCrossover(BiquadType::LowPass, 80.0),
			makeCrossover(BiquadType::LowPass, 80.0)
		},
		0.0));

	state.paths.push_back(makePath(
		"RearL",
		PathKind::Main,
		{{"RL", 1.0}},
		0.0,
		{makeCrossover(BiquadType::HighPass, 100.0)},
		2.0));

	state.paths.push_back(makePath(
		"RearR",
		PathKind::Main,
		{{"RR", 1.0}},
		0.0,
		{makeCrossover(BiquadType::HighPass, 100.0)},
		2.0));

	state.paths.push_back(makePath(
		"RearBass",
		PathKind::Bass,
		{
			{"RL", -1.0},
			{"RR", -1.0}
		},
		0.0,
		{makeCrossover(BiquadType::LowPass, 60.0)},
		0.0));

	state.paths.push_back(makePath(
		"SourceLFE",
		PathKind::SourceLfe,
		{{"LFE", 1.0}},
		10.0,
		{},
		0.0));

	state.outputMatrix = {
		makeOutput(
			"L",
			{
				{"FrontL", 0.0},
				{"FrontBass", 0.0},
				{"SourceLFE", 0.0}
			}),
		makeOutput(
			"R",
			{
				{"FrontR", 0.0},
				{"FrontBass", 0.0},
				{"SourceLFE", 0.0}
			}),
		makeOutput(
			"LFE",
			{
				{"FrontBass", -14.0},
				{"SourceLFE", -14.0},
				{"RearBass", 0.0}
			}),
		makeOutput(
			"RL",
			{
				{"RearL", 0.0}
			}),
		makeOutput(
			"RR",
			{
				{"RearR", 0.0}
			})
	};

	state.headroom.mode = HeadroomMode::Auto;
	state.headroom.manualTrimDb = 0.0;

	// Plain ASCII on purpose: profile names travel through config lines that
	// are edited in tools with unpredictable code pages.
	state.metadata.profileName = "Issue #246 - Front/Rear 4.1";
	state.metadata.creatingApp = "EqualizerAPO-XT";
	state.metadata.creatingAppVersion.clear();

	return state;
}

}

bool PresetCreateResult::succeeded() const noexcept
{
	return state.has_value() && error.empty();
}

const std::vector<PresetDescriptor>& builtInPresets()
{
	static const std::vector<PresetDescriptor> presets = {
		{
			kIssue246FrontRear41PresetId,
			"Issue #246 - Front/Rear 4.1",
			"Front and rear subwoofer routing for a five-channel 4.1 layout."
		}
	};

	return presets;
}

PresetCreateResult createBuiltInPreset(std::string_view presetId)
{
	if (presetId == kIssue246FrontRear41PresetId)
	{
		PresetCreateResult result;
		result.state = makeIssue246FrontRear41Preset();
		return result;
	}

	PresetCreateResult result;
	result.error = "Unknown built-in subwoofer-routing preset: ";
	result.error.append(presetId.data(), presetId.size());
	return result;
}

}
