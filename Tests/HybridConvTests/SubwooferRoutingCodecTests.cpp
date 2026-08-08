// SPDX-License-Identifier: MIT

#include "SubwooferRouting/Json.h"
#include "SubwooferRouting/State.h"
#include "SubwooferRouting/StateCodec.h"
#include "Tests/TestHarness.h"

#include <cstddef>
#include <string>
#include <variant>

namespace
{

test::Harness harness("SubwooferRoutingCodecTests");

subroute::SubwooferRoutingState makeCompleteState()
{
	using namespace subroute;

	SubwooferRoutingState state;
	state.layout.channels = {
		{"L", "Left"},
		{"R", "Right"}
	};

	SpeakerGroup group;
	group.id = "front";
	group.displayName = "Front";
	group.mainPathIds = {"main"};
	group.bassPathId.reset();
	state.speakerGroups.push_back(group);

	Path mainPath;
	mainPath.id = "main";
	mainPath.kind = PathKind::Main;
	mainPath.sourceMix = {
		{"L", 1.0},
		{"R", 0.5}
	};
	mainPath.preGainDb = -1.25;
	mainPath.chain.push_back(GainStage{-2.0});
	mainPath.chain.push_back(PolarityStage{true});
	mainPath.chain.push_back(DelayStage{1.5});

	BiquadStage biquadStage;
	biquadStage.filter.type = BiquadType::LowPass;
	biquadStage.filter.frequencyHz = 80.0;
	biquadStage.filter.q = 0.7071067811865476;
	biquadStage.filter.gainDb = 0.0;
	mainPath.chain.push_back(biquadStage);
	mainPath.chain.push_back(EqualizerSlotsStage{});
	mainPath.postGainDb = -0.75;
	state.paths.push_back(mainPath);

	Path sourceLfePath;
	sourceLfePath.id = "source-lfe";
	sourceLfePath.kind = PathKind::SourceLfe;
	sourceLfePath.sourceMix = {
		{"R", 1.0}
	};
	sourceLfePath.preGainDb = 0.0;
	sourceLfePath.chain.push_back(PolarityStage{false});
	sourceLfePath.chain.push_back(DelayStage{0.0});

	EqualizerSlotsStage populatedSlots;
	BiquadFilter peaking;
	peaking.type = BiquadType::Peaking;
	peaking.frequencyHz = 45.0;
	peaking.q = 1.2;
	peaking.gainDb = 3.0;
	populatedSlots.filters.push_back(peaking);
	sourceLfePath.chain.push_back(populatedSlots);
	sourceLfePath.postGainDb = -3.0;
	state.paths.push_back(sourceLfePath);

	OutputMatrixEntry leftOutput;
	leftOutput.targetChannelId = "L";
	leftOutput.mode = OutputMode::Replace;
	leftOutput.terms = {
		{"main", 0.0},
		{"source-lfe", -6.0}
	};
	state.outputMatrix.push_back(leftOutput);

	OutputMatrixEntry rightOutput;
	rightOutput.targetChannelId = "R";
	rightOutput.mode = OutputMode::Add;
	rightOutput.terms = {
		{"main", -1.0}
	};
	state.outputMatrix.push_back(rightOutput);

	state.headroom.mode = HeadroomMode::Manual;
	state.headroom.manualTrimDb = -4.5;

	state.metadata.profileName = "Codec test";
	state.metadata.creatingApp = "HybridConvTests";
	state.metadata.creatingAppVersion = "1.0";

	return state;
}

subroute::Json parseEncodedState()
{
	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(makeCompleteState());
	harness.require(
		encoded.succeeded(),
		"Fixture state must encode");
	harness.require(
		encoded.text.has_value(),
		"Successful fixture encoding must contain text");

	subroute::JsonParseResult parsed =
		subroute::parseJson(*encoded.text);
	harness.require(
		parsed.succeeded(),
		"Encoded fixture must parse");
	harness.require(
		parsed.value.has_value(),
		"Successful fixture parse must contain a document");
	return std::move(*parsed.value);
}

bool hasError(
	const std::vector<subroute::StateCodecError>& errors,
	subroute::StateCodecErrorCode code,
	const std::string& pointer)
{
	for (const subroute::StateCodecError& error : errors)
	{
		if (error.code == code
			&& error.jsonPointer == pointer)
		{
			return true;
		}
	}

	return false;
}

void testCanonicalRoundTrip()
{
	const subroute::StateEncodeResult firstEncoding =
		subroute::encodeStateCanonical(makeCompleteState());

	harness.require(
		firstEncoding.succeeded(),
		"Initial canonical encoding must succeed");
	harness.require(
		firstEncoding.text.has_value(),
		"Initial canonical encoding must contain text");

	const subroute::StateDecodeResult decoded =
		subroute::decodeState(*firstEncoding.text);

	harness.require(
		decoded.succeeded(),
		"Canonical text must decode");
	harness.require(
		decoded.state.has_value(),
		"Successful decoding must contain state");

	const subroute::StateEncodeResult secondEncoding =
		subroute::encodeStateCanonical(*decoded.state);

	harness.require(
		secondEncoding.succeeded(),
		"Decoded state must encode again");
	harness.require(
		secondEncoding.text.has_value(),
		"Second encoding must contain text");
	harness.expectEqual(
		*secondEncoding.text,
		*firstEncoding.text,
		"Encode-decode-encode must be byte-identical");
}

void testMissingMemberPointer()
{
	subroute::Json document = parseEncodedState();
	document.at("metadata").asObject().erase("profileName");

	const subroute::StateDecodeResult decoded =
		subroute::decodeStateDocument(document);

	harness.expectFalse(
		decoded.succeeded(),
		"Missing required member must fail decoding");
	harness.expectTrue(
		hasError(
			decoded.errors,
			subroute::StateCodecErrorCode::MissingMember,
			"/metadata/profileName"),
		"Missing member must report its exact JSON pointer");
}

void testUnknownMemberRejected()
{
	subroute::Json document = parseEncodedState();
	document.at("paths")
		.at(0)
		.asObject()
		.emplace("unexpected", subroute::Json(true));

	const subroute::StateDecodeResult decoded =
		subroute::decodeStateDocument(document);

	harness.expectFalse(
		decoded.succeeded(),
		"Unknown member must fail strict decoding");
	harness.expectTrue(
		hasError(
			decoded.errors,
			subroute::StateCodecErrorCode::UnexpectedMember,
			"/paths/0/unexpected"),
		"Unknown member must report UnexpectedMember");
}

void testBadEnumValue()
{
	subroute::Json document = parseEncodedState();
	document.at("paths").at(1).at("kind") =
		subroute::Json("SourceLfe");

	const subroute::StateDecodeResult decoded =
		subroute::decodeStateDocument(document);

	harness.expectFalse(
		decoded.succeeded(),
		"Incorrectly cased enum value must fail");
	harness.expectTrue(
		hasError(
			decoded.errors,
			subroute::StateCodecErrorCode::InvalidEnumValue,
			"/paths/1/kind"),
		"Bad enum value must report InvalidEnumValue");
}

void testNewerVersionRejected()
{
	subroute::Json document = parseEncodedState();
	document.at("version") = subroute::Json(2.0);

	const subroute::StateMigrationResult migrated =
		subroute::migrateStateDocument(document);

	harness.expectFalse(
		migrated.succeeded(),
		"Version 2 must not migrate as version 1");
	harness.expectTrue(
		hasError(
			migrated.errors,
			subroute::StateCodecErrorCode::UnsupportedNewerVersion,
			"/version"),
		"Version 2 must report UnsupportedNewerVersion");
}

void testOlderVersionRejected()
{
	subroute::Json document = parseEncodedState();
	document.at("version") = subroute::Json(0.0);

	const subroute::StateMigrationResult migrated =
		subroute::migrateStateDocument(document);

	harness.expectFalse(
		migrated.succeeded(),
		"Version 0 must not migrate");
	harness.expectTrue(
		hasError(
			migrated.errors,
			subroute::StateCodecErrorCode::UnsupportedOlderVersion,
			"/version"),
		"Version 0 must report UnsupportedOlderVersion");
}

void testWrongSchemaRejected()
{
	subroute::Json document = parseEncodedState();
	document.at("schema") = subroute::Json("wrong.schema");

	const subroute::StateMigrationResult migrated =
		subroute::migrateStateDocument(document);

	harness.expectFalse(
		migrated.succeeded(),
		"Wrong schema must fail migration");
	harness.expectTrue(
		hasError(
			migrated.errors,
			subroute::StateCodecErrorCode::InvalidSchema,
			"/schema"),
		"Wrong schema must report InvalidSchema");
}

void testAllStageVariantsRoundTrip()
{
	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(makeCompleteState());

	harness.require(
		encoded.succeeded(),
		"Stage-variant fixture must encode");
	harness.require(
		encoded.text.has_value(),
		"Stage-variant encoding must contain text");

	const subroute::StateDecodeResult decoded =
		subroute::decodeState(*encoded.text);

	harness.require(
		decoded.succeeded(),
		"Stage-variant fixture must decode");
	harness.require(
		decoded.state.has_value(),
		"Stage-variant decoding must contain state");
	harness.requireEqual(
		decoded.state->paths.size(),
		static_cast<std::size_t>(2),
		"Both paths must survive round-trip");

	const std::vector<subroute::PathStage>& chain =
		decoded.state->paths[0].chain;

	harness.requireEqual(
		chain.size(),
		static_cast<std::size_t>(5),
		"All five stage variants must survive round-trip");
	harness.expectTrue(
		std::holds_alternative<subroute::GainStage>(chain[0]),
		"First stage must remain GainStage");
	harness.expectTrue(
		std::holds_alternative<subroute::PolarityStage>(chain[1]),
		"Second stage must remain PolarityStage");
	harness.expectTrue(
		std::holds_alternative<subroute::DelayStage>(chain[2]),
		"Third stage must remain DelayStage");
	harness.expectTrue(
		std::holds_alternative<subroute::BiquadStage>(chain[3]),
		"Fourth stage must remain BiquadStage");
	harness.expectTrue(
		std::holds_alternative<subroute::EqualizerSlotsStage>(
			chain[4]),
		"Fifth stage must remain EqualizerSlotsStage");
}

void testEmptyEqSlotsPreserved()
{
	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(makeCompleteState());

	harness.require(
		encoded.succeeded(),
		"Empty-EQ fixture must encode");
	harness.require(
		encoded.text.has_value(),
		"Empty-EQ encoding must contain text");

	const subroute::StateDecodeResult decoded =
		subroute::decodeState(*encoded.text);

	harness.require(
		decoded.succeeded(),
		"Empty-EQ fixture must decode");
	harness.require(
		decoded.state.has_value(),
		"Empty-EQ decoding must contain state");
	harness.requireEqual(
		decoded.state->paths[0].chain.size(),
		static_cast<std::size_t>(5),
		"Main chain must retain all stages");
	harness.require(
		std::holds_alternative<subroute::EqualizerSlotsStage>(
			decoded.state->paths[0].chain[4]),
		"Main chain must retain the EQ-eqSlots variant");

	const subroute::EqualizerSlotsStage& eqSlots =
		std::get<subroute::EqualizerSlotsStage>(
			decoded.state->paths[0].chain[4]);

	harness.expectTrue(
		eqSlots.filters.empty(),
		"Empty eqSlots filters array must remain empty");
}

}

void runSubwooferRoutingCodecTests()
{
	testCanonicalRoundTrip();
	testMissingMemberPointer();
	testUnknownMemberRejected();
	testBadEnumValue();
	testNewerVersionRejected();
	testOlderVersionRejected();
	testWrongSchemaRejected();
	testAllStageVariantsRoundTrip();
	testEmptyEqSlotsPreserved();
	harness.report();
}
