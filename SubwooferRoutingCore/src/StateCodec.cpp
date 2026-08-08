// SPDX-License-Identifier: MIT

#include "SubwooferRouting/StateCodec.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace subroute
{

namespace
{

std::string escapeJsonPointerToken(std::string_view token)
{
	std::string escaped;

	for (const char character : token)
	{
		switch (character)
		{
		case '~':
			escaped += "~0";
			break;
		case '/':
			escaped += "~1";
			break;
		default:
			escaped.push_back(character);
			break;
		}
	}

	return escaped;
}

std::string appendJsonPointer(
	std::string_view parent,
	std::string_view token)
{
	std::string pointer(parent);
	pointer.push_back('/');
	pointer += escapeJsonPointerToken(token);
	return pointer;
}

std::string appendJsonPointer(
	std::string_view parent,
	std::size_t index)
{
	return appendJsonPointer(parent, std::to_string(index));
}

bool isContinuationByte(unsigned char value) noexcept
{
	return (value & 0xc0U) == 0x80U;
}

bool isValidUtf8(std::string_view text) noexcept
{
	std::size_t index = 0;

	while (index < text.size())
	{
		const auto first = static_cast<unsigned char>(text[index]);

		if (first <= 0x7fU)
		{
			++index;
			continue;
		}

		if (first >= 0xc2U && first <= 0xdfU)
		{
			if (index + 1 >= text.size()
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 1])))
			{
				return false;
			}

			index += 2;
			continue;
		}

		if (first == 0xe0U)
		{
			if (index + 2 >= text.size())
			{
				return false;
			}

			const auto second =
				static_cast<unsigned char>(text[index + 1]);
			const auto third =
				static_cast<unsigned char>(text[index + 2]);

			if (second < 0xa0U || second > 0xbfU
				|| !isContinuationByte(third))
			{
				return false;
			}

			index += 3;
			continue;
		}

		if ((first >= 0xe1U && first <= 0xecU)
			|| (first >= 0xeeU && first <= 0xefU))
		{
			if (index + 2 >= text.size()
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 1]))
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 2])))
			{
				return false;
			}

			index += 3;
			continue;
		}

		if (first == 0xedU)
		{
			if (index + 2 >= text.size())
			{
				return false;
			}

			const auto second =
				static_cast<unsigned char>(text[index + 1]);
			const auto third =
				static_cast<unsigned char>(text[index + 2]);

			if (second < 0x80U || second > 0x9fU
				|| !isContinuationByte(third))
			{
				return false;
			}

			index += 3;
			continue;
		}

		if (first == 0xf0U)
		{
			if (index + 3 >= text.size())
			{
				return false;
			}

			const auto second =
				static_cast<unsigned char>(text[index + 1]);

			if (second < 0x90U || second > 0xbfU
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 2]))
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 3])))
			{
				return false;
			}

			index += 4;
			continue;
		}

		if (first >= 0xf1U && first <= 0xf3U)
		{
			if (index + 3 >= text.size()
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 1]))
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 2]))
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 3])))
			{
				return false;
			}

			index += 4;
			continue;
		}

		if (first == 0xf4U)
		{
			if (index + 3 >= text.size())
			{
				return false;
			}

			const auto second =
				static_cast<unsigned char>(text[index + 1]);

			if (second < 0x80U || second > 0x8fU
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 2]))
				|| !isContinuationByte(
					static_cast<unsigned char>(text[index + 3])))
			{
				return false;
			}

			index += 4;
			continue;
		}

		return false;
	}

	return true;
}

StateCodecError makeError(
	StateCodecErrorCode code,
	std::string jsonPointer,
	std::string message,
	std::size_t offset = kNoJsonOffset)
{
	StateCodecError error;
	error.code = code;
	error.offset = offset;
	error.jsonPointer = std::move(jsonPointer);
	error.message = std::move(message);
	return error;
}

bool containsName(
	std::initializer_list<std::string_view> names,
	std::string_view candidate)
{
	for (const std::string_view name : names)
	{
		if (name == candidate)
		{
			return true;
		}
	}

	return false;
}

class StateDecoder
{
public:
	StateDecodeResult decode(
		const Json& document,
		std::uint32_t sourceVersion,
		bool migrated)
	{
		StateDecodeResult result;
		result.sourceVersion = sourceVersion;
		result.migrated = migrated;

		const Json::Object* root = requireObject(document, "");

		if (root == nullptr)
		{
			result.errors = std::move(errors_);
			return result;
		}

		rejectUnexpected(
			*root,
			"",
			{
				"schema",
				"version",
				"layout",
				"speakerGroups",
				"paths",
				"outputMatrix",
				"headroom",
				"metadata"
			});

		SubwooferRoutingState state;
		readRootIdentity(*root, state);
		decodeLayoutMember(*root, state);
		decodeSpeakerGroupsMember(*root, state);
		decodePathsMember(*root, state);
		decodeOutputMatrixMember(*root, state);
		decodeHeadroomMember(*root, state);
		decodeMetadataMember(*root, state);

		result.errors = std::move(errors_);

		if (result.errors.empty())
		{
			result.state = std::move(state);
		}

		return result;
	}

private:
	void addError(
		StateCodecErrorCode code,
		std::string pointer,
		std::string message)
	{
		errors_.push_back(makeError(
			code,
			std::move(pointer),
			std::move(message)));
	}

	const Json* requireMember(
		const Json::Object& object,
		std::string_view name,
		std::string_view parentPointer)
	{
		const Json* member = nullptr;

		const auto iterator = object.find(name);

		if (iterator != object.end())
		{
			member = &iterator->second;
		}

		if (member == nullptr)
		{
			const std::string pointer =
				appendJsonPointer(parentPointer, name);
			addError(
				StateCodecErrorCode::MissingMember,
				pointer,
				"Required member '" + std::string(name) + "' is missing.");
		}

		return member;
	}

	const Json::Object* requireObject(
		const Json& value,
		std::string_view pointer)
	{
		if (!value.isObject())
		{
			addError(
				StateCodecErrorCode::IncorrectType,
				std::string(pointer),
				"Expected an object.");
			return nullptr;
		}

		return &value.asObject();
	}

	const Json::Array* requireArray(
		const Json& value,
		std::string_view pointer)
	{
		if (!value.isArray())
		{
			addError(
				StateCodecErrorCode::IncorrectType,
				std::string(pointer),
				"Expected an array.");
			return nullptr;
		}

		return &value.asArray();
	}

	std::optional<std::string> requireString(
		const Json& value,
		std::string_view pointer)
	{
		if (!value.isString())
		{
			addError(
				StateCodecErrorCode::IncorrectType,
				std::string(pointer),
				"Expected a string.");
			return std::nullopt;
		}

		const std::string& string = value.asString();

		if (!isValidUtf8(string))
		{
			addError(
				StateCodecErrorCode::InvalidUtf8,
				std::string(pointer),
				"String is not valid UTF-8.");
			return std::nullopt;
		}

		return string;
	}

	std::optional<double> requireNumber(
		const Json& value,
		std::string_view pointer)
	{
		if (!value.isNumber())
		{
			addError(
				StateCodecErrorCode::IncorrectType,
				std::string(pointer),
				"Expected a number.");
			return std::nullopt;
		}

		const double number = value.asNumber();

		if (!std::isfinite(number))
		{
			addError(
				StateCodecErrorCode::NonFiniteNumber,
				std::string(pointer),
				"Number must be finite.");
			return std::nullopt;
		}

		return number;
	}

	std::optional<bool> requireBoolean(
		const Json& value,
		std::string_view pointer)
	{
		if (!value.isBoolean())
		{
			addError(
				StateCodecErrorCode::IncorrectType,
				std::string(pointer),
				"Expected a boolean.");
			return std::nullopt;
		}

		return value.asBoolean();
	}

	void rejectUnexpected(
		const Json::Object& object,
		std::string_view parentPointer,
		std::initializer_list<std::string_view> expectedNames)
	{
		for (const auto& member : object)
		{
			if (containsName(expectedNames, member.first))
			{
				continue;
			}

			addError(
				StateCodecErrorCode::UnexpectedMember,
				appendJsonPointer(parentPointer, member.first),
				"Unexpected member '" + member.first + "'.");
		}
	}

	void readStringMember(
		const Json::Object& object,
		std::string_view name,
		std::string_view parentPointer,
		std::string& destination)
	{
		const Json* member =
			requireMember(object, name, parentPointer);

		if (member == nullptr)
		{
			return;
		}

		const std::string pointer =
			appendJsonPointer(parentPointer, name);
		std::optional<std::string> value =
			requireString(*member, pointer);

		if (value.has_value())
		{
			destination = std::move(*value);
		}
	}

	void readNumberMember(
		const Json::Object& object,
		std::string_view name,
		std::string_view parentPointer,
		double& destination)
	{
		const Json* member =
			requireMember(object, name, parentPointer);

		if (member == nullptr)
		{
			return;
		}

		const std::string pointer =
			appendJsonPointer(parentPointer, name);
		const std::optional<double> value =
			requireNumber(*member, pointer);

		if (value.has_value())
		{
			destination = *value;
		}
	}

	void readBooleanMember(
		const Json::Object& object,
		std::string_view name,
		std::string_view parentPointer,
		bool& destination)
	{
		const Json* member =
			requireMember(object, name, parentPointer);

		if (member == nullptr)
		{
			return;
		}

		const std::string pointer =
			appendJsonPointer(parentPointer, name);
		const std::optional<bool> value =
			requireBoolean(*member, pointer);

		if (value.has_value())
		{
			destination = *value;
		}
	}

	std::optional<std::string> readEnumString(
		const Json::Object& object,
		std::string_view name,
		std::string_view parentPointer)
	{
		const Json* member =
			requireMember(object, name, parentPointer);

		if (member == nullptr)
		{
			return std::nullopt;
		}

		return requireString(
			*member,
			appendJsonPointer(parentPointer, name));
	}

	void addInvalidEnum(
		std::string_view pointer,
		std::string_view value)
	{
		addError(
			StateCodecErrorCode::InvalidEnumValue,
			std::string(pointer),
			"Invalid enum value '" + std::string(value) + "'.");
	}

	void readRootIdentity(
		const Json::Object& root,
		SubwooferRoutingState& state)
	{
		const Json* schema = requireMember(root, "schema", "");

		if (schema != nullptr)
		{
			const std::optional<std::string> value =
				requireString(*schema, "/schema");

			if (value.has_value())
			{
				state.schema = std::move(*value);
			}
		}

		const Json* version = requireMember(root, "version", "");

		if (version != nullptr)
		{
			const std::optional<double> value =
				requireNumber(*version, "/version");

			if (value.has_value())
			{
				if (std::floor(*value) != *value)
				{
					addError(
						StateCodecErrorCode::VersionMustBeInteger,
						"/version",
						"Version must be an integer.");
				}
				else if (*value < 0.0
					|| *value
						> static_cast<double>(
							std::numeric_limits<std::uint32_t>::max()))
				{
					addError(
						StateCodecErrorCode::VersionOutOfRange,
						"/version",
						"Version is outside the uint32 range.");
				}
				else
				{
					state.version =
						static_cast<std::uint32_t>(*value);
				}
			}
		}
	}

	void decodeLayoutMember(
		const Json::Object& root,
		SubwooferRoutingState& state)
	{
		const Json* layoutValue =
			requireMember(root, "layout", "");

		if (layoutValue == nullptr)
		{
			return;
		}

		const Json::Object* layout =
			requireObject(*layoutValue, "/layout");

		if (layout == nullptr)
		{
			return;
		}

		rejectUnexpected(*layout, "/layout", {"channels"});

		const Json* channelsValue =
			requireMember(*layout, "channels", "/layout");

		if (channelsValue == nullptr)
		{
			return;
		}

		const Json::Array* channels =
			requireArray(*channelsValue, "/layout/channels");

		if (channels == nullptr)
		{
			return;
		}

		for (std::size_t index = 0; index < channels->size(); ++index)
		{
			const std::string pointer =
				appendJsonPointer("/layout/channels", index);
			const Json::Object* channel =
				requireObject((*channels)[index], pointer);

			if (channel == nullptr)
			{
				continue;
			}

			rejectUnexpected(
				*channel,
				pointer,
				{"id", "displayName"});

			PhysicalChannel decoded;
			readStringMember(*channel, "id", pointer, decoded.id);
			readStringMember(
				*channel,
				"displayName",
				pointer,
				decoded.displayName);
			state.layout.channels.push_back(std::move(decoded));
		}
	}

	void decodeSpeakerGroupsMember(
		const Json::Object& root,
		SubwooferRoutingState& state)
	{
		const Json* groupsValue =
			requireMember(root, "speakerGroups", "");

		if (groupsValue == nullptr)
		{
			return;
		}

		const Json::Array* groups =
			requireArray(*groupsValue, "/speakerGroups");

		if (groups == nullptr)
		{
			return;
		}

		for (std::size_t index = 0; index < groups->size(); ++index)
		{
			const std::string pointer =
				appendJsonPointer("/speakerGroups", index);
			const Json::Object* group =
				requireObject((*groups)[index], pointer);

			if (group == nullptr)
			{
				continue;
			}

			rejectUnexpected(
				*group,
				pointer,
				{
					"id",
					"displayName",
					"mainPathIds",
					"bassPathId"
				});

			SpeakerGroup decoded;
			readStringMember(*group, "id", pointer, decoded.id);
			readStringMember(
				*group,
				"displayName",
				pointer,
				decoded.displayName);

			const Json* mainPathIdsValue =
				requireMember(*group, "mainPathIds", pointer);

			if (mainPathIdsValue != nullptr)
			{
				const std::string arrayPointer =
					appendJsonPointer(pointer, "mainPathIds");
				const Json::Array* ids =
					requireArray(*mainPathIdsValue, arrayPointer);

				if (ids != nullptr)
				{
					for (std::size_t idIndex = 0;
						idIndex < ids->size();
						++idIndex)
					{
						const std::string idPointer =
							appendJsonPointer(arrayPointer, idIndex);
						std::optional<std::string> id =
							requireString((*ids)[idIndex], idPointer);

						if (id.has_value())
						{
							decoded.mainPathIds.push_back(
								std::move(*id));
						}
					}
				}
			}

			const Json* bassPathIdValue =
				requireMember(*group, "bassPathId", pointer);

			if (bassPathIdValue != nullptr)
			{
				const std::string bassPointer =
					appendJsonPointer(pointer, "bassPathId");

				if (bassPathIdValue->isNull())
				{
					decoded.bassPathId.reset();
				}
				else
				{
					std::optional<std::string> id =
						requireString(*bassPathIdValue, bassPointer);

					if (id.has_value())
					{
						decoded.bassPathId = std::move(*id);
					}
				}
			}

			state.speakerGroups.push_back(std::move(decoded));
		}
	}

	std::optional<PathKind> decodePathKind(
		const Json::Object& path,
		std::string_view pointer)
	{
		const std::optional<std::string> value =
			readEnumString(path, "kind", pointer);

		if (!value.has_value())
		{
			return std::nullopt;
		}

		if (*value == "main")
		{
			return PathKind::Main;
		}

		if (*value == "bass")
		{
			return PathKind::Bass;
		}

		if (*value == "sourceLfe")
		{
			return PathKind::SourceLfe;
		}

		addInvalidEnum(
			appendJsonPointer(pointer, "kind"),
			*value);
		return std::nullopt;
	}

	std::optional<BiquadType> decodeBiquadType(
		const Json::Object& filter,
		std::string_view pointer)
	{
		const std::optional<std::string> value =
			readEnumString(filter, "type", pointer);

		if (!value.has_value())
		{
			return std::nullopt;
		}

		if (*value == "highPass")
		{
			return BiquadType::HighPass;
		}

		if (*value == "lowPass")
		{
			return BiquadType::LowPass;
		}

		if (*value == "peaking")
		{
			return BiquadType::Peaking;
		}

		if (*value == "lowShelf")
		{
			return BiquadType::LowShelf;
		}

		if (*value == "highShelf")
		{
			return BiquadType::HighShelf;
		}

		if (*value == "notch")
		{
			return BiquadType::Notch;
		}

		if (*value == "allPass")
		{
			return BiquadType::AllPass;
		}

		addInvalidEnum(
			appendJsonPointer(pointer, "type"),
			*value);
		return std::nullopt;
	}

	BiquadFilter decodeBiquadFilter(
		const Json& value,
		std::string_view pointer)
	{
		BiquadFilter decoded;
		const Json::Object* filter =
			requireObject(value, pointer);

		if (filter == nullptr)
		{
			return decoded;
		}

		rejectUnexpected(
			*filter,
			pointer,
			{"type", "frequencyHz", "q", "gainDb"});

		const std::optional<BiquadType> type =
			decodeBiquadType(*filter, pointer);

		if (type.has_value())
		{
			decoded.type = *type;
		}

		readNumberMember(
			*filter,
			"frequencyHz",
			pointer,
			decoded.frequencyHz);
		readNumberMember(*filter, "q", pointer, decoded.q);
		readNumberMember(
			*filter,
			"gainDb",
			pointer,
			decoded.gainDb);
		return decoded;
	}

	std::optional<PathStage> decodePathStage(
		const Json& value,
		std::string_view pointer)
	{
		const Json::Object* stage =
			requireObject(value, pointer);

		if (stage == nullptr)
		{
			return std::nullopt;
		}

		const std::optional<std::string> type =
			readEnumString(*stage, "type", pointer);

		if (!type.has_value())
		{
			rejectUnexpected(
				*stage,
				pointer,
				{
					"type",
					"gainDb",
					"inverted",
					"milliseconds",
					"filter",
					"filters"
				});
			return std::nullopt;
		}

		if (*type == "gain")
		{
			rejectUnexpected(
				*stage,
				pointer,
				{"type", "gainDb"});

			GainStage decoded;
			readNumberMember(
				*stage,
				"gainDb",
				pointer,
				decoded.gainDb);
			return PathStage(decoded);
		}

		if (*type == "polarity")
		{
			rejectUnexpected(
				*stage,
				pointer,
				{"type", "inverted"});

			PolarityStage decoded;
			readBooleanMember(
				*stage,
				"inverted",
				pointer,
				decoded.inverted);
			return PathStage(decoded);
		}

		if (*type == "delay")
		{
			rejectUnexpected(
				*stage,
				pointer,
				{"type", "milliseconds"});

			DelayStage decoded;
			readNumberMember(
				*stage,
				"milliseconds",
				pointer,
				decoded.milliseconds);
			return PathStage(decoded);
		}

		if (*type == "biquad")
		{
			rejectUnexpected(
				*stage,
				pointer,
				{"type", "filter"});

			BiquadStage decoded;
			const Json* filter =
				requireMember(*stage, "filter", pointer);

			if (filter != nullptr)
			{
				decoded.filter = decodeBiquadFilter(
					*filter,
					appendJsonPointer(pointer, "filter"));
			}

			return PathStage(decoded);
		}

		if (*type == "eqSlots")
		{
			rejectUnexpected(
				*stage,
				pointer,
				{"type", "filters"});

			EqualizerSlotsStage decoded;
			const Json* filtersValue =
				requireMember(*stage, "filters", pointer);

			if (filtersValue != nullptr)
			{
				const std::string filtersPointer =
					appendJsonPointer(pointer, "filters");
				const Json::Array* filters =
					requireArray(*filtersValue, filtersPointer);

				if (filters != nullptr)
				{
					for (std::size_t index = 0;
						index < filters->size();
						++index)
					{
						decoded.filters.push_back(
							decodeBiquadFilter(
								(*filters)[index],
								appendJsonPointer(
									filtersPointer,
									index)));
					}
				}
			}

			return PathStage(std::move(decoded));
		}

		rejectUnexpected(
			*stage,
			pointer,
			{
				"type",
				"gainDb",
				"inverted",
				"milliseconds",
				"filter",
				"filters"
			});
		addInvalidEnum(
			appendJsonPointer(pointer, "type"),
			*type);
		return std::nullopt;
	}

	void decodePathsMember(
		const Json::Object& root,
		SubwooferRoutingState& state)
	{
		const Json* pathsValue =
			requireMember(root, "paths", "");

		if (pathsValue == nullptr)
		{
			return;
		}

		const Json::Array* paths =
			requireArray(*pathsValue, "/paths");

		if (paths == nullptr)
		{
			return;
		}

		for (std::size_t index = 0; index < paths->size(); ++index)
		{
			const std::string pointer =
				appendJsonPointer("/paths", index);
			const Json::Object* path =
				requireObject((*paths)[index], pointer);

			if (path == nullptr)
			{
				continue;
			}

			rejectUnexpected(
				*path,
				pointer,
				{
					"id",
					"kind",
					"sourceMix",
					"preGainDb",
					"chain",
					"postGainDb"
				});

			Path decoded;
			readStringMember(*path, "id", pointer, decoded.id);

			const std::optional<PathKind> kind =
				decodePathKind(*path, pointer);

			if (kind.has_value())
			{
				decoded.kind = *kind;
			}

			const Json* sourceMixValue =
				requireMember(*path, "sourceMix", pointer);

			if (sourceMixValue != nullptr)
			{
				const std::string mixPointer =
					appendJsonPointer(pointer, "sourceMix");
				const Json::Array* sourceMix =
					requireArray(*sourceMixValue, mixPointer);

				if (sourceMix != nullptr)
				{
					for (std::size_t mixIndex = 0;
						mixIndex < sourceMix->size();
						++mixIndex)
					{
						const std::string termPointer =
							appendJsonPointer(mixPointer, mixIndex);
						const Json::Object* term =
							requireObject(
								(*sourceMix)[mixIndex],
								termPointer);

						if (term == nullptr)
						{
							continue;
						}

						rejectUnexpected(
							*term,
							termPointer,
							{"inputChannelId", "gainLinear"});

						SourceMixTerm decodedTerm;
						readStringMember(
							*term,
							"inputChannelId",
							termPointer,
							decodedTerm.inputChannelId);
						readNumberMember(
							*term,
							"gainLinear",
							termPointer,
							decodedTerm.gainLinear);
						decoded.sourceMix.push_back(
							std::move(decodedTerm));
					}
				}
			}

			readNumberMember(
				*path,
				"preGainDb",
				pointer,
				decoded.preGainDb);

			const Json* chainValue =
				requireMember(*path, "chain", pointer);

			if (chainValue != nullptr)
			{
				const std::string chainPointer =
					appendJsonPointer(pointer, "chain");
				const Json::Array* chain =
					requireArray(*chainValue, chainPointer);

				if (chain != nullptr)
				{
					for (std::size_t stageIndex = 0;
						stageIndex < chain->size();
						++stageIndex)
					{
						std::optional<PathStage> stage =
							decodePathStage(
								(*chain)[stageIndex],
								appendJsonPointer(
									chainPointer,
									stageIndex));

						if (stage.has_value())
						{
							decoded.chain.push_back(
								std::move(*stage));
						}
					}
				}
			}

			readNumberMember(
				*path,
				"postGainDb",
				pointer,
				decoded.postGainDb);
			state.paths.push_back(std::move(decoded));
		}
	}

	std::optional<OutputMode> decodeOutputMode(
		const Json::Object& entry,
		std::string_view pointer)
	{
		const std::optional<std::string> value =
			readEnumString(entry, "mode", pointer);

		if (!value.has_value())
		{
			return std::nullopt;
		}

		if (*value == "replace")
		{
			return OutputMode::Replace;
		}

		if (*value == "add")
		{
			return OutputMode::Add;
		}

		addInvalidEnum(
			appendJsonPointer(pointer, "mode"),
			*value);
		return std::nullopt;
	}

	void decodeOutputMatrixMember(
		const Json::Object& root,
		SubwooferRoutingState& state)
	{
		const Json* matrixValue =
			requireMember(root, "outputMatrix", "");

		if (matrixValue == nullptr)
		{
			return;
		}

		const Json::Array* matrix =
			requireArray(*matrixValue, "/outputMatrix");

		if (matrix == nullptr)
		{
			return;
		}

		for (std::size_t index = 0; index < matrix->size(); ++index)
		{
			const std::string pointer =
				appendJsonPointer("/outputMatrix", index);
			const Json::Object* entry =
				requireObject((*matrix)[index], pointer);

			if (entry == nullptr)
			{
				continue;
			}

			rejectUnexpected(
				*entry,
				pointer,
				{"targetChannelId", "mode", "terms"});

			OutputMatrixEntry decoded;
			readStringMember(
				*entry,
				"targetChannelId",
				pointer,
				decoded.targetChannelId);

			const std::optional<OutputMode> mode =
				decodeOutputMode(*entry, pointer);

			if (mode.has_value())
			{
				decoded.mode = *mode;
			}

			const Json* termsValue =
				requireMember(*entry, "terms", pointer);

			if (termsValue != nullptr)
			{
				const std::string termsPointer =
					appendJsonPointer(pointer, "terms");
				const Json::Array* terms =
					requireArray(*termsValue, termsPointer);

				if (terms != nullptr)
				{
					for (std::size_t termIndex = 0;
						termIndex < terms->size();
						++termIndex)
					{
						const std::string termPointer =
							appendJsonPointer(termsPointer, termIndex);
						const Json::Object* term =
							requireObject(
								(*terms)[termIndex],
								termPointer);

						if (term == nullptr)
						{
							continue;
						}

						rejectUnexpected(
							*term,
							termPointer,
							{"sourcePathId", "gainDb"});

						OutputMatrixTerm decodedTerm;
						readStringMember(
							*term,
							"sourcePathId",
							termPointer,
							decodedTerm.sourcePathId);
						readNumberMember(
							*term,
							"gainDb",
							termPointer,
							decodedTerm.gainDb);
						decoded.terms.push_back(
							std::move(decodedTerm));
					}
				}
			}

			state.outputMatrix.push_back(std::move(decoded));
		}
	}

	std::optional<HeadroomMode> decodeHeadroomMode(
		const Json::Object& headroom,
		std::string_view pointer)
	{
		const std::optional<std::string> value =
			readEnumString(headroom, "mode", pointer);

		if (!value.has_value())
		{
			return std::nullopt;
		}

		if (*value == "auto")
		{
			return HeadroomMode::Auto;
		}

		if (*value == "manual")
		{
			return HeadroomMode::Manual;
		}

		addInvalidEnum(
			appendJsonPointer(pointer, "mode"),
			*value);
		return std::nullopt;
	}

	void decodeHeadroomMember(
		const Json::Object& root,
		SubwooferRoutingState& state)
	{
		const Json* headroomValue =
			requireMember(root, "headroom", "");

		if (headroomValue == nullptr)
		{
			return;
		}

		const Json::Object* headroom =
			requireObject(*headroomValue, "/headroom");

		if (headroom == nullptr)
		{
			return;
		}

		rejectUnexpected(
			*headroom,
			"/headroom",
			{"mode", "manualTrimDb"});

		const std::optional<HeadroomMode> mode =
			decodeHeadroomMode(*headroom, "/headroom");

		if (mode.has_value())
		{
			state.headroom.mode = *mode;
		}

		readNumberMember(
			*headroom,
			"manualTrimDb",
			"/headroom",
			state.headroom.manualTrimDb);
	}

	void decodeMetadataMember(
		const Json::Object& root,
		SubwooferRoutingState& state)
	{
		const Json* metadataValue =
			requireMember(root, "metadata", "");

		if (metadataValue == nullptr)
		{
			return;
		}

		const Json::Object* metadata =
			requireObject(*metadataValue, "/metadata");

		if (metadata == nullptr)
		{
			return;
		}

		rejectUnexpected(
			*metadata,
			"/metadata",
			{
				"profileName",
				"creatingApp",
				"creatingAppVersion"
			});

		readStringMember(
			*metadata,
			"profileName",
			"/metadata",
			state.metadata.profileName);
		readStringMember(
			*metadata,
			"creatingApp",
			"/metadata",
			state.metadata.creatingApp);
		readStringMember(
			*metadata,
			"creatingAppVersion",
			"/metadata",
			state.metadata.creatingAppVersion);
	}

	std::vector<StateCodecError> errors_;
};

class StateEncoder
{
public:
	Json encode(const SubwooferRoutingState& state)
	{
		Json::Object root;
		root.emplace("schema", Json(state.schema));
		root.emplace(
			"version",
			Json(static_cast<double>(state.version)));
		root.emplace("layout", encodeLayout(state.layout));
		root.emplace(
			"speakerGroups",
			encodeSpeakerGroups(state.speakerGroups));
		root.emplace("paths", encodePaths(state.paths));
		root.emplace(
			"outputMatrix",
			encodeOutputMatrix(state.outputMatrix));
		root.emplace("headroom", encodeHeadroom(state.headroom));
		root.emplace("metadata", encodeMetadata(state.metadata));
		return Json(std::move(root));
	}

	std::vector<StateCodecError> takeErrors()
	{
		return std::move(errors_);
	}

private:
	void addInvalidEnum(std::string pointer)
	{
		errors_.push_back(makeError(
			StateCodecErrorCode::InvalidEnumValue,
			std::move(pointer),
			"Enum contains an unsupported underlying value."));
	}

	std::string encodePathKind(
		PathKind kind,
		std::string_view pointer)
	{
		switch (kind)
		{
		case PathKind::Main:
			return "main";
		case PathKind::Bass:
			return "bass";
		case PathKind::SourceLfe:
			return "sourceLfe";
		}

		addInvalidEnum(std::string(pointer));
		return "";
	}

	std::string encodeBiquadType(
		BiquadType type,
		std::string_view pointer)
	{
		switch (type)
		{
		case BiquadType::HighPass:
			return "highPass";
		case BiquadType::LowPass:
			return "lowPass";
		case BiquadType::Peaking:
			return "peaking";
		case BiquadType::LowShelf:
			return "lowShelf";
		case BiquadType::HighShelf:
			return "highShelf";
		case BiquadType::Notch:
			return "notch";
		case BiquadType::AllPass:
			return "allPass";
		}

		addInvalidEnum(std::string(pointer));
		return "";
	}

	std::string encodeOutputMode(
		OutputMode mode,
		std::string_view pointer)
	{
		switch (mode)
		{
		case OutputMode::Replace:
			return "replace";
		case OutputMode::Add:
			return "add";
		}

		addInvalidEnum(std::string(pointer));
		return "";
	}

	std::string encodeHeadroomMode(
		HeadroomMode mode,
		std::string_view pointer)
	{
		switch (mode)
		{
		case HeadroomMode::Auto:
			return "auto";
		case HeadroomMode::Manual:
			return "manual";
		}

		addInvalidEnum(std::string(pointer));
		return "";
	}

	static Json encodeLayout(const ChannelLayout& layout)
	{
		Json::Array channels;

		for (const PhysicalChannel& channel : layout.channels)
		{
			Json::Object encoded;
			encoded.emplace("id", Json(channel.id));
			encoded.emplace(
				"displayName",
				Json(channel.displayName));
			channels.emplace_back(std::move(encoded));
		}

		Json::Object encoded;
		encoded.emplace("channels", Json(std::move(channels)));
		return Json(std::move(encoded));
	}

	static Json encodeSpeakerGroups(
		const std::vector<SpeakerGroup>& groups)
	{
		Json::Array encodedGroups;

		for (const SpeakerGroup& group : groups)
		{
			Json::Array mainPathIds;

			for (const std::string& id : group.mainPathIds)
			{
				mainPathIds.emplace_back(id);
			}

			Json::Object encoded;
			encoded.emplace("id", Json(group.id));
			encoded.emplace(
				"displayName",
				Json(group.displayName));
			encoded.emplace(
				"mainPathIds",
				Json(std::move(mainPathIds)));

			if (group.bassPathId.has_value())
			{
				encoded.emplace(
					"bassPathId",
					Json(*group.bassPathId));
			}
			else
			{
				encoded.emplace("bassPathId", Json());
			}

			encodedGroups.emplace_back(std::move(encoded));
		}

		return Json(std::move(encodedGroups));
	}

	Json encodeBiquadFilter(
		const BiquadFilter& filter,
		std::string_view pointer)
	{
		Json::Object encoded;
		encoded.emplace(
			"type",
			Json(encodeBiquadType(
				filter.type,
				appendJsonPointer(pointer, "type"))));
		encoded.emplace(
			"frequencyHz",
			Json(filter.frequencyHz));
		encoded.emplace("q", Json(filter.q));
		encoded.emplace("gainDb", Json(filter.gainDb));
		return Json(std::move(encoded));
	}

	Json encodePathStage(
		const PathStage& stage,
		std::string_view pointer)
	{
		if (stage.valueless_by_exception())
		{
			errors_.push_back(makeError(
				StateCodecErrorCode::CanonicalSerializationFailed,
				std::string(pointer),
				"Path-stage variant is valueless."));
			return Json(Json::Object());
		}

		return std::visit(
			[this, pointer](const auto& concreteStage) -> Json
			{
				using Stage =
					std::decay_t<decltype(concreteStage)>;

				Json::Object encoded;

				if constexpr (std::is_same_v<Stage, GainStage>)
				{
					encoded.emplace("type", Json("gain"));
					encoded.emplace(
						"gainDb",
						Json(concreteStage.gainDb));
				}
				else if constexpr (
					std::is_same_v<Stage, PolarityStage>)
				{
					encoded.emplace("type", Json("polarity"));
					encoded.emplace(
						"inverted",
						Json(concreteStage.inverted));
				}
				else if constexpr (
					std::is_same_v<Stage, DelayStage>)
				{
					encoded.emplace("type", Json("delay"));
					encoded.emplace(
						"milliseconds",
						Json(concreteStage.milliseconds));
				}
				else if constexpr (
					std::is_same_v<Stage, BiquadStage>)
				{
					encoded.emplace("type", Json("biquad"));
					encoded.emplace(
						"filter",
						encodeBiquadFilter(
							concreteStage.filter,
							appendJsonPointer(pointer, "filter")));
				}
				else
				{
					Json::Array filters;

					for (std::size_t index = 0;
						index < concreteStage.filters.size();
						++index)
					{
						filters.push_back(
							encodeBiquadFilter(
								concreteStage.filters[index],
								appendJsonPointer(
									appendJsonPointer(
										pointer,
										"filters"),
									index)));
					}

					encoded.emplace("type", Json("eqSlots"));
					encoded.emplace(
						"filters",
						Json(std::move(filters)));
				}

				return Json(std::move(encoded));
			},
			stage);
	}

	Json encodePaths(const std::vector<Path>& paths)
	{
		Json::Array encodedPaths;

		for (std::size_t pathIndex = 0;
			pathIndex < paths.size();
			++pathIndex)
		{
			const Path& path = paths[pathIndex];
			const std::string pointer =
				appendJsonPointer("/paths", pathIndex);

			Json::Array sourceMix;

			for (const SourceMixTerm& term : path.sourceMix)
			{
				Json::Object encodedTerm;
				encodedTerm.emplace(
					"inputChannelId",
					Json(term.inputChannelId));
				encodedTerm.emplace(
					"gainLinear",
					Json(term.gainLinear));
				sourceMix.emplace_back(std::move(encodedTerm));
			}

			Json::Array chain;

			for (std::size_t stageIndex = 0;
				stageIndex < path.chain.size();
				++stageIndex)
			{
				chain.push_back(
					encodePathStage(
						path.chain[stageIndex],
						appendJsonPointer(
							appendJsonPointer(pointer, "chain"),
							stageIndex)));
			}

			Json::Object encoded;
			encoded.emplace("id", Json(path.id));
			encoded.emplace(
				"kind",
				Json(encodePathKind(
					path.kind,
					appendJsonPointer(pointer, "kind"))));
			encoded.emplace(
				"sourceMix",
				Json(std::move(sourceMix)));
			encoded.emplace(
				"preGainDb",
				Json(path.preGainDb));
			encoded.emplace("chain", Json(std::move(chain)));
			encoded.emplace(
				"postGainDb",
				Json(path.postGainDb));
			encodedPaths.emplace_back(std::move(encoded));
		}

		return Json(std::move(encodedPaths));
	}

	Json encodeOutputMatrix(
		const std::vector<OutputMatrixEntry>& matrix)
	{
		Json::Array encodedMatrix;

		for (std::size_t entryIndex = 0;
			entryIndex < matrix.size();
			++entryIndex)
		{
			const OutputMatrixEntry& entry =
				matrix[entryIndex];
			const std::string pointer =
				appendJsonPointer("/outputMatrix", entryIndex);
			Json::Array terms;

			for (const OutputMatrixTerm& term : entry.terms)
			{
				Json::Object encodedTerm;
				encodedTerm.emplace(
					"sourcePathId",
					Json(term.sourcePathId));
				encodedTerm.emplace(
					"gainDb",
					Json(term.gainDb));
				terms.emplace_back(std::move(encodedTerm));
			}

			Json::Object encoded;
			encoded.emplace(
				"targetChannelId",
				Json(entry.targetChannelId));
			encoded.emplace(
				"mode",
				Json(encodeOutputMode(
					entry.mode,
					appendJsonPointer(pointer, "mode"))));
			encoded.emplace("terms", Json(std::move(terms)));
			encodedMatrix.emplace_back(std::move(encoded));
		}

		return Json(std::move(encodedMatrix));
	}

	Json encodeHeadroom(const HeadroomSettings& headroom)
	{
		Json::Object encoded;
		encoded.emplace(
			"mode",
			Json(encodeHeadroomMode(
				headroom.mode,
				"/headroom/mode")));
		encoded.emplace(
			"manualTrimDb",
			Json(headroom.manualTrimDb));
		return Json(std::move(encoded));
	}

	static Json encodeMetadata(const StateMetadata& metadata)
	{
		Json::Object encoded;
		encoded.emplace(
			"profileName",
			Json(metadata.profileName));
		encoded.emplace(
			"creatingApp",
			Json(metadata.creatingApp));
		encoded.emplace(
			"creatingAppVersion",
			Json(metadata.creatingAppVersion));
		return Json(std::move(encoded));
	}

	std::vector<StateCodecError> errors_;
};

StateDecodeResult decodeMigratedDocument(
	const Json& document,
	std::uint32_t sourceVersion,
	bool migrated)
{
	StateDecoder decoder;
	return decoder.decode(document, sourceVersion, migrated);
}

}

bool StateMigrationResult::succeeded() const noexcept
{
	return document.has_value() && errors.empty();
}

bool StateDecodeResult::succeeded() const noexcept
{
	return state.has_value() && errors.empty();
}

bool StateEncodeResult::succeeded() const noexcept
{
	return text.has_value() && errors.empty();
}

StateMigrationResult migrateStateDocument(const Json& document)
{
	StateMigrationResult result;

	if (!document.isObject())
	{
		result.errors.push_back(makeError(
			StateCodecErrorCode::RootMustBeObject,
			"",
			"State document root must be an object."));
		return result;
	}

	const Json* schema = document.find("schema");

	if (schema == nullptr)
	{
		result.errors.push_back(makeError(
			StateCodecErrorCode::MissingSchema,
			"/schema",
			"Required schema member is missing."));
	}
	else if (!schema->isString())
	{
		result.errors.push_back(makeError(
			StateCodecErrorCode::SchemaMustBeString,
			"/schema",
			"Schema must be a string."));
	}
	else if (schema->asString() != kSubwooferRoutingSchema)
	{
		result.errors.push_back(makeError(
			StateCodecErrorCode::InvalidSchema,
			"/schema",
			"Schema must be '" +
				std::string(kSubwooferRoutingSchema) + "'."));
	}

	bool versionIsValid = false;
	const Json* version = document.find("version");

	if (version == nullptr)
	{
		result.errors.push_back(makeError(
			StateCodecErrorCode::MissingVersion,
			"/version",
			"Required version member is missing."));
	}
	else if (!version->isNumber())
	{
		result.errors.push_back(makeError(
			StateCodecErrorCode::VersionMustBeInteger,
			"/version",
			"Version must be an integer."));
	}
	else
	{
		const double numericVersion = version->asNumber();

		if (!std::isfinite(numericVersion)
			|| std::floor(numericVersion) != numericVersion)
		{
			result.errors.push_back(makeError(
				StateCodecErrorCode::VersionMustBeInteger,
				"/version",
				"Version must be a finite integer."));
		}
		else if (numericVersion < 0.0
			|| numericVersion
				> static_cast<double>(
					std::numeric_limits<std::uint32_t>::max()))
		{
			result.errors.push_back(makeError(
				StateCodecErrorCode::VersionOutOfRange,
				"/version",
				"Version is outside the uint32 range."));
		}
		else
		{
			result.sourceVersion =
				static_cast<std::uint32_t>(numericVersion);
			versionIsValid = true;
		}
	}

	if (versionIsValid)
	{
		if (result.sourceVersion
			< kOldestSupportedSchemaVersion)
		{
			result.errors.push_back(makeError(
				StateCodecErrorCode::UnsupportedOlderVersion,
				"/version",
				"Schema version "
					+ std::to_string(result.sourceVersion)
					+ " is older than the oldest supported version "
					+ std::to_string(
						kOldestSupportedSchemaVersion)
					+ "."));
		}
		else if (result.sourceVersion
			> kSubwooferRoutingSchemaVersion)
		{
			result.errors.push_back(makeError(
				StateCodecErrorCode::UnsupportedNewerVersion,
				"/version",
				"Schema version "
					+ std::to_string(result.sourceVersion)
					+ " is newer than the supported version "
					+ std::to_string(
						kSubwooferRoutingSchemaVersion)
					+ "."));
		}
	}

	if (!result.errors.empty())
	{
		return result;
	}

	result.document = document;
	result.migrated = false;
	return result;
}

StateDecodeResult decodeStateDocument(const Json& document)
{
	StateMigrationResult migration =
		migrateStateDocument(document);

	if (!migration.succeeded())
	{
		StateDecodeResult result;
		result.errors = std::move(migration.errors);
		result.sourceVersion = migration.sourceVersion;
		result.migrated = migration.migrated;
		return result;
	}

	return decodeMigratedDocument(
		*migration.document,
		migration.sourceVersion,
		migration.migrated);
}

StateDecodeResult decodeState(
	std::string_view text,
	const JsonParseOptions& parseOptions)
{
	JsonParseResult parseResult =
		parseJson(text, parseOptions);

	if (!parseResult.succeeded())
	{
		StateDecodeResult result;

		if (parseResult.error.has_value())
		{
			result.errors.push_back(makeError(
				StateCodecErrorCode::JsonParseError,
				"",
				parseResult.error->message,
				parseResult.error->offset));
		}
		else
		{
			result.errors.push_back(makeError(
				StateCodecErrorCode::JsonParseError,
				"",
				"JSON parsing failed without an error detail."));
		}

		return result;
	}

	StateMigrationResult migration =
		migrateStateDocument(*parseResult.value);

	if (!migration.succeeded())
	{
		StateDecodeResult result;
		result.errors = std::move(migration.errors);
		result.sourceVersion = migration.sourceVersion;
		result.migrated = migration.migrated;
		return result;
	}

	return decodeMigratedDocument(
		*migration.document,
		migration.sourceVersion,
		migration.migrated);
}

StateEncodeResult encodeStateCanonical(
	const SubwooferRoutingState& state)
{
	StateEncodeResult result;
	StateEncoder encoder;
	Json document = encoder.encode(state);
	result.errors = encoder.takeErrors();

	if (!result.errors.empty())
	{
		return result;
	}

	JsonSerializeResult serialization =
		serializeJson(document);

	if (!serialization.succeeded())
	{
		if (serialization.error.has_value())
		{
			result.errors.push_back(makeError(
				StateCodecErrorCode::CanonicalSerializationFailed,
				serialization.error->jsonPointer,
				serialization.error->message));
		}
		else
		{
			result.errors.push_back(makeError(
				StateCodecErrorCode::CanonicalSerializationFailed,
				"",
				"Canonical JSON serialization failed without an error detail."));
		}

		return result;
	}

	result.text = std::move(serialization.text);
	return result;
}

}
