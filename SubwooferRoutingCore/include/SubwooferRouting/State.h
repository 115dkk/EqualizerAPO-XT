// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace subroute
{

inline constexpr char kSubwooferRoutingSchema[] =
	"equalizerapo.xt.subwoofer-routing";

inline constexpr std::uint32_t kSubwooferRoutingSchemaVersion = 1;

struct PhysicalChannel
{
	std::string id;
	std::string displayName;
};

struct ChannelLayout
{
	std::vector<PhysicalChannel> channels;
};

enum class PathKind
{
	Main,
	Bass,
	SourceLfe
};

struct SourceMixTerm
{
	std::string inputChannelId;
	double gainLinear = 1.0;
};

enum class BiquadType
{
	HighPass,
	LowPass,
	Peaking,
	LowShelf,
	HighShelf,
	Notch,
	AllPass
};

/*
	Each BiquadFilter describes one second-order section.

	For HighPass, LowPass, Notch, and AllPass, q is the section Q.
	For Peaking, q is the peaking-EQ Q.
	For LowShelf and HighShelf, q is the RBJ shelf slope parameter S.

	gainDb is used by Peaking, LowShelf, and HighShelf and is ignored by
	HighPass, LowPass, Notch, and AllPass.
*/
struct BiquadFilter
{
	BiquadType type = BiquadType::Peaking;
	double frequencyHz = 1000.0;
	double q = 0.7071067811865476;
	double gainDb = 0.0;
};

struct GainStage
{
	double gainDb = 0.0;
};

struct PolarityStage
{
	bool inverted = false;
};

struct DelayStage
{
	double milliseconds = 0.0;
};

struct BiquadStage
{
	BiquadFilter filter;
};

/*
	An EqualizerSlotsStage is an explicitly ordered list of imported or
	user-created EQ biquad sections. Every valid path contains exactly one
	EqualizerSlotsStage, even when its filters vector is empty.
*/
struct EqualizerSlotsStage
{
	std::vector<BiquadFilter> filters;
};

using PathStage = std::variant<
	GainStage,
	PolarityStage,
	DelayStage,
	BiquadStage,
	EqualizerSlotsStage>;

/*
	Path processing order is:

	1. Weighted source mix using original physical inputs.
	2. preGainDb.
	3. Every chain element in vector order.
	4. postGainDb.

	Every valid path contains exactly one PolarityStage, exactly one
	DelayStage, and exactly one EqualizerSlotsStage. Additional GainStage and
	BiquadStage elements may occur anywhere in the chain.
*/
struct Path
{
	std::string id;
	PathKind kind = PathKind::Main;
	std::vector<SourceMixTerm> sourceMix;
	double preGainDb = 0.0;
	std::vector<PathStage> chain;
	double postGainDb = 0.0;
};

struct SpeakerGroup
{
	std::string id;
	std::string displayName;
	std::vector<std::string> mainPathIds;
	std::optional<std::string> bassPathId;
};

enum class OutputMode
{
	Replace,
	Add
};

struct OutputMatrixTerm
{
	std::string sourcePathId;
	double gainDb = 0.0;
};

struct OutputMatrixEntry
{
	std::string targetChannelId;
	OutputMode mode = OutputMode::Replace;
	std::vector<OutputMatrixTerm> terms;
};

enum class HeadroomMode
{
	Auto,
	Manual
};

struct HeadroomSettings
{
	HeadroomMode mode = HeadroomMode::Auto;
	double manualTrimDb = 0.0;
};

struct StateMetadata
{
	std::string profileName;
	std::string creatingApp;
	std::string creatingAppVersion;
};

struct SubwooferRoutingState
{
	std::string schema = kSubwooferRoutingSchema;
	std::uint32_t version = kSubwooferRoutingSchemaVersion;
	ChannelLayout layout;
	std::vector<SpeakerGroup> speakerGroups;
	std::vector<Path> paths;
	std::vector<OutputMatrixEntry> outputMatrix;
	HeadroomSettings headroom;
	StateMetadata metadata;
};

}
