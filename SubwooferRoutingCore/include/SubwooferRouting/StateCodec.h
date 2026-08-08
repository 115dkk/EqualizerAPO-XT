// SPDX-License-Identifier: MIT

#pragma once

#include "Json.h"
#include "State.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace subroute
{

inline constexpr std::uint32_t kOldestSupportedSchemaVersion = 1;
inline constexpr std::size_t kNoJsonOffset = static_cast<std::size_t>(-1);

enum class StateCodecErrorCode
{
	JsonParseError,
	RootMustBeObject,
	MissingSchema,
	SchemaMustBeString,
	InvalidSchema,
	MissingVersion,
	VersionMustBeInteger,
	VersionOutOfRange,
	UnsupportedOlderVersion,
	UnsupportedNewerVersion,
	MigrationFailed,
	MissingMember,
	UnexpectedMember,
	IncorrectType,
	InvalidEnumValue,
	InvalidUtf8,
	NonFiniteNumber,
	NumberOutOfRange,
	CanonicalSerializationFailed
};

struct StateCodecError
{
	StateCodecErrorCode code = StateCodecErrorCode::IncorrectType;
	std::size_t offset = kNoJsonOffset;
	std::string jsonPointer;
	std::string message;
};

struct StateMigrationResult
{
	std::optional<Json> document;
	std::vector<StateCodecError> errors;
	std::uint32_t sourceVersion = 0;
	bool migrated = false;

	bool succeeded() const noexcept;
};

struct StateDecodeResult
{
	std::optional<SubwooferRoutingState> state;
	std::vector<StateCodecError> errors;
	std::uint32_t sourceVersion = 0;
	bool migrated = false;

	bool succeeded() const noexcept;
};

struct StateEncodeResult
{
	std::optional<std::string> text;
	std::vector<StateCodecError> errors;

	bool succeeded() const noexcept;
};

/*
	Version 1 canonical document shape:

	{
		"schema": "equalizerapo.xt.subwoofer-routing",
		"version": 1,
		"layout": {
			"channels": [
				{
					"id": string,
					"displayName": string
				}
			]
		},
		"speakerGroups": [
			{
				"id": string,
				"displayName": string,
				"mainPathIds": [ string, ... ],
				"bassPathId": string | null
			}
		],
		"paths": [
			{
				"id": string,
				"kind": "main" | "bass" | "sourceLfe",
				"sourceMix": [
					{
						"inputChannelId": string,
						"gainLinear": number
					}
				],
				"preGainDb": number,
				"chain": [
					{
						"type": "gain",
						"gainDb": number
					},
					{
						"type": "polarity",
						"inverted": boolean
					},
					{
						"type": "delay",
						"milliseconds": number
					},
					{
						"type": "biquad",
						"filter": {
							"type": "highPass" | "lowPass" |
								"peaking" | "lowShelf" |
								"highShelf" | "notch" |
								"allPass",
							"frequencyHz": number,
							"q": number,
							"gainDb": number
						}
					},
					{
						"type": "eqSlots",
						"filters": [
							{
								"type": biquad-type-string,
								"frequencyHz": number,
								"q": number,
								"gainDb": number
							}
						]
					}
				],
				"postGainDb": number
			}
		],
		"outputMatrix": [
			{
				"targetChannelId": string,
				"mode": "replace" | "add",
				"terms": [
					{
						"sourcePathId": string,
						"gainDb": number
					}
				]
			}
		],
		"headroom": {
			"mode": "auto" | "manual",
			"manualTrimDb": number
		},
		"metadata": {
			"profileName": string,
			"creatingApp": string,
			"creatingAppVersion": string
		}
	}

	Version 1 decoding is strict: all listed members are required and unknown
	members are rejected. Arrays preserve their input order. A successful
	migration produces a strict current-version document before typed decoding.

	Canonical serialization emits all members, including null bassPathId and
	empty arrays. It uses compact Json serialization with lexicographically
	sorted object keys and shortest round-tripping double representations.
*/
StateMigrationResult migrateStateDocument(const Json& document);

StateDecodeResult decodeStateDocument(const Json& document);

StateDecodeResult decodeState(
	std::string_view text,
	const JsonParseOptions& parseOptions = JsonParseOptions());

StateEncodeResult encodeStateCanonical(const SubwooferRoutingState& state);

}
