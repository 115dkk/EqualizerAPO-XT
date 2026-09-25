// SPDX-License-Identifier: MIT

#include "SubwooferRouting/Compiler.h"
#include "SubwooferRouting/Json.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/StateCodec.h"
#include "Tests/TestHarness.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace
{

test::Harness harness("SubwooferRoutingJsonTests");

void testLiteralsNestingAndEscapes()
{
	// The non-BMP musical symbol U+1D11E is spelled as an escaped surrogate
	// pair so this source file stays pure ASCII (the test binary compiles
	// without /utf-8, where a raw UTF-8 literal would be mis-decoded).
	const auto result = subroute::parseJson(
		R"({"array":[null,true,false,{"text":"line\n\t\"\\\/\uD834\uDD1E"}]})");

	harness.require(result.succeeded(), "nested document should parse");
	harness.require(result.value->isObject(), "root should be an object");

	const subroute::Json& array = result.value->at("array");
	harness.require(array.isArray(), "array member should be an array");
	harness.requireEqual(
		array.asArray().size(),
		std::size_t(4),
		"array should contain four elements");

	harness.expectTrue(array.at(0).isNull(), "null literal should parse");
	harness.expectTrue(
		array.at(1).isBoolean() && array.at(1).asBoolean(),
		"true literal should parse");
	harness.expectTrue(
		array.at(2).isBoolean() && !array.at(2).asBoolean(),
		"false literal should parse");

	const subroute::Json& text = array.at(3).at("text");
	harness.require(text.isString(), "nested text member should be a string");

	std::string expected = "line\n\t";
	expected += "\"\\/";
	expected += "\xF0\x9D\x84\x9E";
	harness.expectEqual(
		text.asString(),
		expected,
		"string escapes and surrogate pair should decode");
}

bool hasInvalidUtf8Diagnostic(
	const subroute::ValidationResult& validation)
{
	for (const subroute::ValidationDiagnostic& diagnostic : validation.diagnostics)
	{
		if (diagnostic.code == subroute::DiagnosticCode::InvalidUtf8)
			return true;
	}
	return false;
}

void testInvalidUtf8Rejected()
{
	std::vector<std::string> malformed;
	malformed.emplace_back("\xC3(", 2);
	malformed.emplace_back("\xC0\x80", 2);
	malformed.emplace_back("\xED\xA0\x80", 3);
	malformed.emplace_back("\xF4\x90\x80\x80", 4);
	malformed.emplace_back("\xF0\x9F\x92", 3);
	const std::vector<std::string> valid = {
		std::string("ASCII"),
		std::string("\xC2\x80", 2),
		std::string("\xE0\xA0\x80", 3),
		std::string("\xED\x9F\xBF", 3),
		std::string("\xF0\x90\x80\x80", 4),
		std::string("\xF4\x8F\xBF\xBF", 4)
	};

	for (const std::string& accepted : valid)
	{
		const std::string text = std::string("\"") + accepted + "\"";
		harness.expectTrue(subroute::parseJson(text).succeeded(),
			"valid UTF-8 should be accepted by JSON parsing");

		const subroute::PresetCreateResult preset =
			subroute::createBuiltInPreset(
				subroute::kIssue246FrontRear41PresetId);
		harness.require(preset.succeeded(),
			"valid UTF-8 fixture should be created");
		subroute::SubwooferRoutingState state = *preset.state;
		state.metadata.profileName = accepted;
		harness.expectFalse(hasInvalidUtf8Diagnostic(subroute::validate(state)),
			"state validation should accept valid UTF-8");
		harness.expectTrue(subroute::encodeStateCanonical(state).succeeded(),
			"state encoding should accept valid UTF-8");
	}

	for (const std::string& invalid : malformed)
	{
		const std::string text = std::string("\"") + invalid + "\"";
		const auto parsed = subroute::parseJson(text);
		harness.expectFalse(parsed.succeeded(),
			"malformed UTF-8 should be rejected by JSON parsing");
		harness.require(parsed.error.has_value(),
			"malformed UTF-8 should report a JSON error");
		harness.expectTrue(
			parsed.error->code == subroute::JsonParseErrorCode::InvalidUtf8,
			"malformed UTF-8 should use InvalidUtf8");

		const subroute::PresetCreateResult preset =
			subroute::createBuiltInPreset(
				subroute::kIssue246FrontRear41PresetId);
		harness.require(preset.succeeded(),
			"UTF-8 validation fixture should be created");
		subroute::SubwooferRoutingState state = *preset.state;
		state.metadata.profileName = invalid;
		harness.expectTrue(
			hasInvalidUtf8Diagnostic(subroute::validate(state)),
			"state validation should reject malformed UTF-8");

		const subroute::StateEncodeResult encoded =
			subroute::encodeStateCanonical(state);
		harness.expectFalse(encoded.succeeded(),
			"state encoding should reject malformed UTF-8");

		const subroute::StateEncodeResult validEncoding =
			subroute::encodeStateCanonical(*preset.state);
		harness.require(validEncoding.succeeded(),
			"valid UTF-8 codec fixture should encode");
		subroute::JsonParseResult validDocument =
			subroute::parseJson(*validEncoding.text);
		harness.require(validDocument.succeeded(),
			"valid UTF-8 codec fixture should parse");
		validDocument.value->at("metadata").at("profileName") =
			subroute::Json(invalid);
		const subroute::StateDecodeResult decoded =
			subroute::decodeStateDocument(*validDocument.value);
		harness.expectFalse(decoded.succeeded(),
			"state codec should reject malformed UTF-8");
	}

	std::string continuationFailure;
	continuationFailure.push_back('"');
	continuationFailure.push_back(static_cast<char>(0xC3));
	continuationFailure.push_back('(');
	continuationFailure.push_back('"');
	const auto result = subroute::parseJson(continuationFailure);
	harness.require(result.error.has_value(),
		"invalid continuation byte should report an error");
	harness.expectEqual(
		result.error->offset,
		std::size_t(2),
		"UTF-8 error should identify the invalid continuation byte");
}

void testDuplicateKeyRejected()
{
	const auto result = subroute::parseJson(R"({"a":1,"a":2})");
	harness.expectFalse(result.succeeded(), "duplicate object key should be rejected");
	harness.require(result.error.has_value(), "duplicate key should report an error");
	harness.expectTrue(
		result.error->code == subroute::JsonParseErrorCode::DuplicateObjectKey,
		"duplicate key should use DuplicateObjectKey");
	harness.expectEqual(
		result.error->offset,
		std::size_t(7),
		"duplicate-key offset should identify the second key");
}

void testTrailingContentRejected()
{
	const auto result = subroute::parseJson("null false");
	harness.expectFalse(result.succeeded(), "trailing content should be rejected");
	harness.require(result.error.has_value(), "trailing content should report an error");
	harness.expectTrue(
		result.error->code == subroute::JsonParseErrorCode::TrailingContent,
		"trailing content should use TrailingContent");
	harness.expectEqual(
		result.error->offset,
		std::size_t(5),
		"trailing-content offset should identify the next token");
}

void testTrailingCommaOffset()
{
	const auto result = subroute::parseJson("[0,]");
	harness.expectFalse(result.succeeded(), "trailing array comma should be rejected");
	harness.require(result.error.has_value(), "trailing comma should report an error");
	harness.expectTrue(
		result.error->code == subroute::JsonParseErrorCode::TrailingComma,
		"trailing comma should use TrailingComma");
	harness.expectEqual(
		result.error->offset,
		std::size_t(3),
		"trailing-comma offset should identify the closing bracket");
}

void testDepthLimit()
{
	subroute::JsonParseOptions options;
	options.maximumDepth = 2;

	const auto accepted = subroute::parseJson("[[0]]", options);
	harness.expectTrue(
		accepted.succeeded(),
		"document at the configured container depth should parse");

	const auto rejected = subroute::parseJson("[[[0]]]", options);
	harness.expectFalse(
		rejected.succeeded(),
		"document beyond the configured container depth should fail");
	harness.require(
		rejected.error.has_value(),
		"depth-limit failure should report an error");
	harness.expectTrue(
		rejected.error->code == subroute::JsonParseErrorCode::MaximumDepthExceeded,
		"depth-limit failure should use MaximumDepthExceeded");
	harness.expectEqual(
		rejected.error->offset,
		std::size_t(2),
		"depth-limit offset should identify the excessive container");
}

void testDoubleRoundTrips()
{
	const double values[] = {
		0.1,
		1e-300,
		-0.0,
		9007199254740992.0
	};

	for (const double expected : values)
	{
		const auto serialized = subroute::serializeJson(subroute::Json(expected));
		harness.require(
			serialized.succeeded(),
			"finite double should serialize");

		const auto parsed = subroute::parseJson(*serialized.text);
		harness.require(
			parsed.succeeded(),
			"serialized double should parse");
		harness.require(
			parsed.value->isNumber(),
			"serialized double should parse as a number");
		harness.expectTrue(
			parsed.value->asNumber() == expected,
			"double should round-trip exactly");

		if (expected == 0.0 && std::signbit(expected))
		{
			harness.expectTrue(
				!serialized.text->empty() && serialized.text->front() == '-',
				"negative zero serialization should preserve its sign");
			harness.expectTrue(
				std::signbit(parsed.value->asNumber()),
				"negative zero parse should preserve its sign");
		}
	}
}

void testCanonicalKeyOrdering()
{
	subroute::Json::Object object;
	object.emplace("z", subroute::Json(1.0));
	object.emplace("a", subroute::Json(2.0));
	object.emplace("m", subroute::Json(3.0));

	const auto result =
		subroute::serializeJson(subroute::Json(std::move(object)));
	harness.require(result.succeeded(), "ordered object should serialize");
	harness.expectEqual(
		*result.text,
		std::string(R"({"a":2,"m":3,"z":1})"),
		"object keys should serialize in lexicographic order");
}

void testMixedDocumentRoundTrip()
{
	const auto original = subroute::parseJson(
		R"({"z":[null,true,0.1,"text"],"a":{"nested":false,"unicode":"\u20AC"},"n":-12.5e2})");
	harness.require(original.succeeded(), "mixed document should parse");

	const auto serialized = subroute::serializeJson(*original.value);
	harness.require(serialized.succeeded(), "mixed document should serialize");

	const auto reparsed = subroute::parseJson(*serialized.text);
	harness.require(reparsed.succeeded(), "serialized mixed document should parse");
	harness.expectTrue(
		*reparsed.value == *original.value,
		"parse-serialize-parse should preserve a mixed document");
}

}

void runSubwooferRoutingJsonTests()
{
	testLiteralsNestingAndEscapes();
	testInvalidUtf8Rejected();
	testDuplicateKeyRejected();
	testTrailingContentRejected();
	testTrailingCommaOffset();
	testDepthLimit();
	testDoubleRoundTrips();
	testCanonicalKeyOrdering();
	testMixedDocumentRoundTrip();
	harness.report();
}
