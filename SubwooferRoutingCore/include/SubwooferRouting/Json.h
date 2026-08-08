// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace subroute
{

enum class JsonType
{
	Null,
	Boolean,
	Number,
	String,
	Array,
	Object
};

class Json
{
public:
	using Array = std::vector<Json>;
	using Object = std::map<std::string, Json, std::less<>>;

	Json() noexcept;
	Json(std::nullptr_t) noexcept;
	explicit Json(bool value) noexcept;
	explicit Json(double value) noexcept;
	explicit Json(std::string value);
	explicit Json(std::string_view value);
	explicit Json(const char* value);
	explicit Json(Array value);
	explicit Json(Object value);

	JsonType type() const noexcept;

	bool isNull() const noexcept;
	bool isBoolean() const noexcept;
	bool isNumber() const noexcept;
	bool isString() const noexcept;
	bool isArray() const noexcept;
	bool isObject() const noexcept;

	bool& asBoolean();
	const bool& asBoolean() const;

	double& asNumber();
	const double& asNumber() const;

	std::string& asString();
	const std::string& asString() const;

	Array& asArray();
	const Array& asArray() const;

	Object& asObject();
	const Object& asObject() const;

	Json& at(std::size_t index);
	const Json& at(std::size_t index) const;

	Json& at(std::string_view key);
	const Json& at(std::string_view key) const;

	Json* find(std::string_view key) noexcept;
	const Json* find(std::string_view key) const noexcept;

	friend bool operator==(const Json& left, const Json& right);
	friend bool operator!=(const Json& left, const Json& right);

private:
	using Storage = std::variant<
		std::nullptr_t,
		bool,
		double,
		std::string,
		Array,
		Object>;

	Storage value_;
};

enum class JsonParseErrorCode
{
	UnexpectedEnd,
	UnexpectedToken,
	InvalidLiteral,
	InvalidNumber,
	NumberOutOfRange,
	InvalidStringEscape,
	InvalidUnicodeEscape,
	InvalidUtf8,
	UnescapedControlCharacter,
	ExpectedColon,
	ExpectedCommaOrEnd,
	TrailingComma,
	DuplicateObjectKey,
	TrailingContent,
	MaximumDepthExceeded
};

struct JsonParseError
{
	JsonParseErrorCode code = JsonParseErrorCode::UnexpectedToken;
	std::size_t offset = 0;
	std::string message;
};

struct JsonParseOptions
{
	std::size_t maximumDepth = 256;
	bool rejectDuplicateObjectKeys = true;
};

struct JsonParseResult
{
	std::optional<Json> value;
	std::optional<JsonParseError> error;

	bool succeeded() const noexcept;
};

enum class JsonSerializeErrorCode
{
	NonFiniteNumber,
	InvalidUtf8
};

struct JsonSerializeError
{
	JsonSerializeErrorCode code = JsonSerializeErrorCode::NonFiniteNumber;
	std::string jsonPointer;
	std::string message;
};

struct JsonSerializeResult
{
	std::optional<std::string> text;
	std::optional<JsonSerializeError> error;

	bool succeeded() const noexcept;
};

/*
	Parsing accepts strict RFC 8259 JSON only. Comments, trailing commas,
	duplicate object members, malformed UTF-8, and non-finite numbers are
	rejected. Error offsets are zero-based UTF-8 byte offsets.

	Serialization is compact and deterministic. Object keys are emitted in
	lexicographic UTF-8 byte order because Json::Object is ordered. Doubles are
	emitted using the shortest decimal representation that round-trips to the
	same double, including the sign of negative zero.
*/
JsonParseResult parseJson(
	std::string_view text,
	const JsonParseOptions& options = JsonParseOptions());

JsonSerializeResult serializeJson(const Json& value);

}
