// SPDX-License-Identifier: MIT

#include "BassManagement/Json.h"
#include "BassManagement/State.h"
#include "BassManagement/StateCodec.h"
#include "Tests/TestHarness.h"

#include <cstddef>
#include <string>
#include <variant>

namespace
{

test::Harness harness("BassManagementCodecTests");

bassmgmt::BassManagementState makeCompleteState()
{
	using namespace bassmgmt;

	BassManagementState state;
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

bassmgmt::Json parseEncodedState()
{
	const bassmgmt::StateEncodeResult encoded =
		bassmgmt::encodeStateCanonical(makeCompleteState());
	harness.require(
		encoded.succeeded(),
		"Fixture state must encode");
	harness.require(
		encoded.text.has_value(),
		"Successful fixture encoding must contain text");

	bassmgmt::JsonParseResult parsed =
		bassmgmt::parseJson(*encoded.text);
	harness.require(
		parsed.succeeded(),
		"Encoded fixture must parse");
	harness.require(
		parsed.value.has_value(),
		"Successful fixture parse must contain a document");
	return std::move(*parsed.value);
}

bool hasError(
	const std::vector<bassmgmt::StateCodecError>& errors,
	bassmgmt::StateCodecErrorCode code,
	const std::string& pointer)
{
	for (const bassmgmt::StateCodecError& error : errors)
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
	const bassmgmt::StateEncodeResult firstEncoding =
		bassmgmt::encodeStateCanonical(makeCompleteState());

	harness.require(
		firstEncoding.succeeded(),
		"Initial canonical encoding must succeed");
	harness.require(
		firstEncoding.text.has_value(),
		"Initial canonical encoding must contain text");

	const bassmgmt::StateDecodeResult decoded =
		bassmgmt::decodeState(*firstEncoding.text);

	harness.require(
		decoded.succeeded(),
		"Canonical text must decode");
	harness.require(
		decoded.state.has_value(),
		"Successful decoding must contain state");

	const bassmgmt::StateEncodeResult secondEncoding =
		bassmgmt::encodeStateCanonical(*decoded.state);

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
	bassmgmt::Json document = parseEncodedState();
	document.at("metadata").asObject().erase("profileName");

	const bassmgmt::StateDecodeResult decoded =
		bassmgmt::decodeStateDocument(document);

	harness.expectFalse(
		decoded.succeeded(),
		"Missing required member must fail decoding");
	harness.expectTrue(
		hasError(
			decoded.errors,
			bassmgmt::StateCodecErrorCode::MissingMember,
			"/metadata/profileName"),
		"Missing member must report its exact JSON pointer");
}

void testUnknownMemberRejected()
{
	bassmgmt::Json document = parseEncodedState();
	document.at("paths")
		.at(0)
		.asObject()
		.emplace("unexpected", bassmgmt::Json(true));

	const bassmgmt::StateDecodeResult decoded =
		bassmgmt::decodeStateDocument(document);

	harness.expectFalse(
		decoded.succeeded(),
		"Unknown member must fail strict decoding");
	harness.expectTrue(
		hasError(
			decoded.errors,
			bassmgmt::StateCodecErrorCode::UnexpectedMember,
			"/paths/0/unexpected"),
		"Unknown member must report UnexpectedMember");
}

void testBadEnumValue()
{
	bassmgmt::Json document = parseEncodedState();
	document.at("paths").at(1).at("kind") =
		bassmgmt::Json("SourceLfe");

	const bassmgmt::StateDecodeResult decoded =
		bassmgmt::decodeStateDocument(document);

	harness.expectFalse(
		decoded.succeeded(),
		"Incorrectly cased enum value must fail");
	harness.expectTrue(
		hasError(
			decoded.errors,
			bassmgmt::StateCodecErrorCode::InvalidEnumValue,
			"/paths/1/kind"),
		"Bad enum value must report InvalidEnumValue");
}

void testNewerVersionRejected()
{
	bassmgmt::Json document = parseEncodedState();
	document.at("version") = bassmgmt::Json(2.0);

	const bassmgmt::StateMigrationResult migrated =
		bassmgmt::migrateStateDocument(document);

	harness.expectFalse(
		migrated.succeeded(),
		"Version 2 must not migrate as version 1");
	harness.expectTrue(
		hasError(
			migrated.errors,
			bassmgmt::StateCodecErrorCode::UnsupportedNewerVersion,
			"/version"),
		"Version 2 must report UnsupportedNewerVersion");
}

void testOlderVersionRejected()
{
	bassmgmt::Json document = parseEncodedState();
	document.at("version") = bassmgmt::Json(0.0);

	const bassmgmt::StateMigrationResult migrated =
		bassmgmt::migrateStateDocument(document);

	harness.expectFalse(
		migrated.succeeded(),
		"Version 0 must not migrate");
	harness.expectTrue(
		hasError(
			migrated.errors,
			bassmgmt::StateCodecErrorCode::UnsupportedOlderVersion,
			"/version"),
		"Version 0 must report UnsupportedOlderVersion");
}

void testWrongSchemaRejected()
{
	bassmgmt::Json document = parseEncodedState();
	document.at("schema") = bassmgmt::Json("wrong.schema");

	const bassmgmt::StateMigrationResult migrated =
		bassmgmt::migrateStateDocument(document);

	harness.expectFalse(
		migrated.succeeded(),
		"Wrong schema must fail migration");
	harness.expectTrue(
		hasError(
			migrated.errors,
			bassmgmt::StateCodecErrorCode::InvalidSchema,
			"/schema"),
		"Wrong schema must report InvalidSchema");
}

void testAllStageVariantsRoundTrip()
{
	const bassmgmt::StateEncodeResult encoded =
		bassmgmt::encodeStateCanonical(makeCompleteState());

	harness.require(
		encoded.succeeded(),
		"Stage-variant fixture must encode");
	harness.require(
		encoded.text.has_value(),
		"Stage-variant encoding must contain text");

	const bassmgmt::StateDecodeResult decoded =
		bassmgmt::decodeState(*encoded.text);

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

	const std::vector<bassmgmt::PathStage>& chain =
		decoded.state->paths[0].chain;

	harness.requireEqual(
		chain.size(),
		static_cast<std::size_t>(5),
		"All five stage variants must survive round-trip");
	harness.expectTrue(
		std::holds_alternative<bassmgmt::GainStage>(chain[0]),
		"First stage must remain GainStage");
	harness.expectTrue(
		std::holds_alternative<bassmgmt::PolarityStage>(chain[1]),
		"Second stage must remain PolarityStage");
	harness.expectTrue(
		std::holds_alternative<bassmgmt::DelayStage>(chain[2]),
		"Third stage must remain DelayStage");
	harness.expectTrue(
		std::holds_alternative<bassmgmt::BiquadStage>(chain[3]),
		"Fourth stage must remain BiquadStage");
	harness.expectTrue(
		std::holds_alternative<bassmgmt::EqualizerSlotsStage>(
			chain[4]),
		"Fifth stage must remain EqualizerSlotsStage");
}

void testEmptyEqSlotsPreserved()
{
	const bassmgmt::StateEncodeResult encoded =
		bassmgmt::encodeStateCanonical(makeCompleteState());

	harness.require(
		encoded.succeeded(),
		"Empty-EQ fixture must encode");
	harness.require(
		encoded.text.has_value(),
		"Empty-EQ encoding must contain text");

	const bassmgmt::StateDecodeResult decoded =
		bassmgmt::decodeState(*encoded.text);

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
		std::holds_alternative<bassmgmt::EqualizerSlotsStage>(
			decoded.state->paths[0].chain[4]),
		"Main chain must retain the EQ-slots variant");

	const bassmgmt::EqualizerSlotsStage& slots =
		std::get<bassmgmt::EqualizerSlotsStage>(
			decoded.state->paths[0].chain[4]);

	harness.expectTrue(
		slots.filters.empty(),
		"Empty eqSlots filters array must remain empty");
}

}

void runBassManagementCodecTests()
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
