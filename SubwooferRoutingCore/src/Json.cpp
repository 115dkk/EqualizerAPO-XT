// SPDX-License-Identifier: MIT

#include "SubwooferRouting/Json.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace subroute
{

Json::Json() noexcept
	: value_(nullptr)
{
}

Json::Json(std::nullptr_t) noexcept
	: value_(nullptr)
{
}

Json::Json(bool value) noexcept
	: value_(value)
{
}

Json::Json(double value) noexcept
	: value_(value)
{
}

Json::Json(std::string value)
	: value_(std::move(value))
{
}

Json::Json(std::string_view value)
	: value_(std::string(value))
{
}

Json::Json(const char* value)
	: value_(std::string(value))
{
}

Json::Json(Array value)
	: value_(std::move(value))
{
}

Json::Json(Object value)
	: value_(std::move(value))
{
}

JsonType Json::type() const noexcept
{
	switch (value_.index())
	{
	case 0:
		return JsonType::Null;
	case 1:
		return JsonType::Boolean;
	case 2:
		return JsonType::Number;
	case 3:
		return JsonType::String;
	case 4:
		return JsonType::Array;
	case 5:
		return JsonType::Object;
	default:
		return JsonType::Null;
	}
}

bool Json::isNull() const noexcept
{
	return std::holds_alternative<std::nullptr_t>(value_);
}

bool Json::isBoolean() const noexcept
{
	return std::holds_alternative<bool>(value_);
}

bool Json::isNumber() const noexcept
{
	return std::holds_alternative<double>(value_);
}

bool Json::isString() const noexcept
{
	return std::holds_alternative<std::string>(value_);
}

bool Json::isArray() const noexcept
{
	return std::holds_alternative<Array>(value_);
}

bool Json::isObject() const noexcept
{
	return std::holds_alternative<Object>(value_);
}

bool& Json::asBoolean()
{
	return std::get<bool>(value_);
}

const bool& Json::asBoolean() const
{
	return std::get<bool>(value_);
}

double& Json::asNumber()
{
	return std::get<double>(value_);
}

const double& Json::asNumber() const
{
	return std::get<double>(value_);
}

std::string& Json::asString()
{
	return std::get<std::string>(value_);
}

const std::string& Json::asString() const
{
	return std::get<std::string>(value_);
}

Json::Array& Json::asArray()
{
	return std::get<Array>(value_);
}

const Json::Array& Json::asArray() const
{
	return std::get<Array>(value_);
}

Json::Object& Json::asObject()
{
	return std::get<Object>(value_);
}

const Json::Object& Json::asObject() const
{
	return std::get<Object>(value_);
}

Json& Json::at(std::size_t index)
{
	return asArray().at(index);
}

const Json& Json::at(std::size_t index) const
{
	return asArray().at(index);
}

Json& Json::at(std::string_view key)
{
	auto iterator = asObject().find(key);
	if (iterator == asObject().end())
	{
		throw std::out_of_range("JSON object key not found");
	}
	return iterator->second;
}

const Json& Json::at(std::string_view key) const
{
	auto iterator = asObject().find(key);
	if (iterator == asObject().end())
	{
		throw std::out_of_range("JSON object key not found");
	}
	return iterator->second;
}

Json* Json::find(std::string_view key) noexcept
{
	if (!isObject())
	{
		return nullptr;
	}

	auto iterator = asObject().find(key);
	return iterator == asObject().end() ? nullptr : &iterator->second;
}

const Json* Json::find(std::string_view key) const noexcept
{
	if (!isObject())
	{
		return nullptr;
	}

	auto iterator = asObject().find(key);
	return iterator == asObject().end() ? nullptr : &iterator->second;
}

bool operator==(const Json& left, const Json& right)
{
	return left.value_ == right.value_;
}

bool operator!=(const Json& left, const Json& right)
{
	return !(left == right);
}

bool JsonParseResult::succeeded() const noexcept
{
	return value.has_value() && !error.has_value();
}

bool JsonSerializeResult::succeeded() const noexcept
{
	return text.has_value() && !error.has_value();
}

namespace
{

bool validateUtf8Sequence(
	std::string_view text,
	std::size_t offset,
	std::size_t& sequenceLength,
	std::size_t& failureOffset)
{
	const auto byteAt = [&text](std::size_t index)
	{
		return static_cast<unsigned char>(text[index]);
	};

	const unsigned char first = byteAt(offset);
	if (first <= 0x7F)
	{
		sequenceLength = 1;
		return true;
	}

	std::size_t requiredLength = 0;
	unsigned char secondMinimum = 0x80;
	unsigned char secondMaximum = 0xBF;

	if (first >= 0xC2 && first <= 0xDF)
	{
		requiredLength = 2;
	}
	else if (first == 0xE0)
	{
		requiredLength = 3;
		secondMinimum = 0xA0;
	}
	else if (first >= 0xE1 && first <= 0xEC)
	{
		requiredLength = 3;
	}
	else if (first == 0xED)
	{
		requiredLength = 3;
		secondMaximum = 0x9F;
	}
	else if (first >= 0xEE && first <= 0xEF)
	{
		requiredLength = 3;
	}
	else if (first == 0xF0)
	{
		requiredLength = 4;
		secondMinimum = 0x90;
	}
	else if (first >= 0xF1 && first <= 0xF3)
	{
		requiredLength = 4;
	}
	else if (first == 0xF4)
	{
		requiredLength = 4;
		secondMaximum = 0x8F;
	}
	else
	{
		failureOffset = offset;
		return false;
	}

	if (offset + 1 >= text.size())
	{
		failureOffset = text.size();
		return false;
	}

	const unsigned char second = byteAt(offset + 1);
	if (second < secondMinimum || second > secondMaximum)
	{
		failureOffset = offset + 1;
		return false;
	}

	for (std::size_t index = 2; index < requiredLength; ++index)
	{
		if (offset + index >= text.size())
		{
			failureOffset = text.size();
			return false;
		}

		const unsigned char continuation = byteAt(offset + index);
		if (continuation < 0x80 || continuation > 0xBF)
		{
			failureOffset = offset + index;
			return false;
		}
	}

	sequenceLength = requiredLength;
	return true;
}

bool validateUtf8(std::string_view text, std::size_t& failureOffset)
{
	std::size_t offset = 0;
	while (offset < text.size())
	{
		std::size_t sequenceLength = 0;
		if (!validateUtf8Sequence(text, offset, sequenceLength, failureOffset))
		{
			return false;
		}
		offset += sequenceLength;
	}
	return true;
}

int hexadecimalValue(char character)
{
	if (character >= '0' && character <= '9')
	{
		return character - '0';
	}
	if (character >= 'a' && character <= 'f')
	{
		return character - 'a' + 10;
	}
	if (character >= 'A' && character <= 'F')
	{
		return character - 'A' + 10;
	}
	return -1;
}

void appendUtf8(std::string& output, std::uint32_t codePoint)
{
	if (codePoint <= 0x7F)
	{
		output.push_back(static_cast<char>(codePoint));
	}
	else if (codePoint <= 0x7FF)
	{
		output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
		output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
	}
	else if (codePoint <= 0xFFFF)
	{
		output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
		output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
	}
	else
	{
		output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
		output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
	}
}

class JsonParser
{
public:
	JsonParser(std::string_view text, const JsonParseOptions& options)
		: text_(text), options_(options)
	{
	}

	JsonParseResult parse()
	{
		JsonParseResult result;
		skipWhitespace();

		if (offset_ == text_.size())
		{
			fail(
				JsonParseErrorCode::UnexpectedEnd,
				offset_,
				"Expected a JSON value");
			result.error = error_;
			return result;
		}

		Json value;
		if (!parseValue(value, 0))
		{
			result.error = error_;
			return result;
		}

		skipWhitespace();
		if (offset_ != text_.size())
		{
			failSyntax(
				JsonParseErrorCode::TrailingContent,
				offset_,
				"Unexpected content after the JSON value");
			result.error = error_;
			return result;
		}

		result.value = std::move(value);
		return result;
	}

private:
	bool fail(
		JsonParseErrorCode code,
		std::size_t offset,
		std::string message)
	{
		if (!error_.has_value())
		{
			error_ = JsonParseError{
				code,
				offset,
				std::move(message)
			};
		}
		return false;
	}

	bool failSyntax(
		JsonParseErrorCode code,
		std::size_t offset,
		std::string message)
	{
		if (offset < text_.size()
			&& static_cast<unsigned char>(text_[offset]) >= 0x80)
		{
			std::size_t sequenceLength = 0;
			std::size_t failureOffset = offset;
			if (!validateUtf8Sequence(
				text_,
				offset,
				sequenceLength,
				failureOffset))
			{
				return fail(
					JsonParseErrorCode::InvalidUtf8,
					failureOffset,
					"Malformed UTF-8 sequence");
			}
		}

		return fail(code, offset, std::move(message));
	}

	void skipWhitespace()
	{
		while (offset_ < text_.size())
		{
			const char character = text_[offset_];
			if (character != ' '
				&& character != '\t'
				&& character != '\n'
				&& character != '\r')
			{
				break;
			}
			++offset_;
		}
	}

	bool parseValue(Json& output, std::size_t containerDepth)
	{
		if (offset_ == text_.size())
		{
			return fail(
				JsonParseErrorCode::UnexpectedEnd,
				offset_,
				"Expected a JSON value");
		}

		switch (text_[offset_])
		{
		case 'n':
			return parseLiteral("null", Json(nullptr), output);
		case 't':
			return parseLiteral("true", Json(true), output);
		case 'f':
			return parseLiteral("false", Json(false), output);
		case '"':
		{
			std::string value;
			if (!parseString(value))
			{
				return false;
			}
			output = Json(std::move(value));
			return true;
		}
		case '[':
			return parseArray(output, containerDepth);
		case '{':
			return parseObject(output, containerDepth);
		default:
			if (text_[offset_] == '-'
				|| (text_[offset_] >= '0' && text_[offset_] <= '9'))
			{
				return parseNumber(output);
			}
			return failSyntax(
				JsonParseErrorCode::UnexpectedToken,
				offset_,
				"Unexpected token while parsing a JSON value");
		}
	}

	bool parseLiteral(
		std::string_view literal,
		Json value,
		Json& output)
	{
		const std::size_t start = offset_;
		for (std::size_t index = 0; index < literal.size(); ++index)
		{
			const std::size_t current = start + index;
			if (current >= text_.size())
			{
				return fail(
					JsonParseErrorCode::UnexpectedEnd,
					text_.size(),
					"Unexpected end of JSON literal");
			}

			if (text_[current] != literal[index])
			{
				return failSyntax(
					JsonParseErrorCode::InvalidLiteral,
					current,
					"Invalid JSON literal");
			}
		}

		offset_ += literal.size();
		output = std::move(value);
		return true;
	}

	bool parseNumber(Json& output)
	{
		const std::size_t start = offset_;

		if (text_[offset_] == '-')
		{
			++offset_;
			if (offset_ == text_.size())
			{
				return fail(
					JsonParseErrorCode::InvalidNumber,
					offset_,
					"Expected a digit after the minus sign");
			}
		}

		if (text_[offset_] == '0')
		{
			++offset_;
			if (offset_ < text_.size()
				&& text_[offset_] >= '0'
				&& text_[offset_] <= '9')
			{
				return fail(
					JsonParseErrorCode::InvalidNumber,
					offset_,
					"Leading zeroes are not permitted in JSON numbers");
			}
		}
		else if (text_[offset_] >= '1' && text_[offset_] <= '9')
		{
			while (offset_ < text_.size()
				&& text_[offset_] >= '0'
				&& text_[offset_] <= '9')
			{
				++offset_;
			}
		}
		else
		{
			return failSyntax(
				JsonParseErrorCode::InvalidNumber,
				offset_,
				"Expected a digit in the JSON number");
		}

		if (offset_ < text_.size() && text_[offset_] == '.')
		{
			++offset_;
			if (offset_ == text_.size()
				|| text_[offset_] < '0'
				|| text_[offset_] > '9')
			{
				return failSyntax(
					JsonParseErrorCode::InvalidNumber,
					offset_,
					"Expected a digit after the decimal point");
			}

			while (offset_ < text_.size()
				&& text_[offset_] >= '0'
				&& text_[offset_] <= '9')
			{
				++offset_;
			}
		}

		if (offset_ < text_.size()
			&& (text_[offset_] == 'e' || text_[offset_] == 'E'))
		{
			++offset_;
			if (offset_ < text_.size()
				&& (text_[offset_] == '+' || text_[offset_] == '-'))
			{
				++offset_;
			}

			if (offset_ == text_.size()
				|| text_[offset_] < '0'
				|| text_[offset_] > '9')
			{
				return failSyntax(
					JsonParseErrorCode::InvalidNumber,
					offset_,
					"Expected a digit in the number exponent");
			}

			while (offset_ < text_.size()
				&& text_[offset_] >= '0'
				&& text_[offset_] <= '9')
			{
				++offset_;
			}
		}

		double value = 0.0;
		const char* first = text_.data() + start;
		const char* last = text_.data() + offset_;
		const auto conversion = std::from_chars(first, last, value);

		if (conversion.ec == std::errc::result_out_of_range
			|| !std::isfinite(value))
		{
			return fail(
				JsonParseErrorCode::NumberOutOfRange,
				start,
				"JSON number is outside the supported double range");
		}

		if (conversion.ec != std::errc()
			|| conversion.ptr != last)
		{
			return fail(
				JsonParseErrorCode::InvalidNumber,
				start,
				"JSON number could not be converted to double");
		}

		output = Json(value);
		return true;
	}

	bool parseString(std::string& output)
	{
		++offset_;
		output.clear();

		while (offset_ < text_.size())
		{
			const unsigned char character =
				static_cast<unsigned char>(text_[offset_]);

			if (character == '"')
			{
				++offset_;
				return true;
			}

			if (character < 0x20)
			{
				return fail(
					JsonParseErrorCode::UnescapedControlCharacter,
					offset_,
					"Unescaped control character in JSON string");
			}

			if (character == '\\')
			{
				if (!parseStringEscape(output))
				{
					return false;
				}
				continue;
			}

			if (character <= 0x7F)
			{
				output.push_back(static_cast<char>(character));
				++offset_;
				continue;
			}

			std::size_t sequenceLength = 0;
			std::size_t failureOffset = offset_;
			if (!validateUtf8Sequence(
				text_,
				offset_,
				sequenceLength,
				failureOffset))
			{
				return fail(
					JsonParseErrorCode::InvalidUtf8,
					failureOffset,
					"Malformed UTF-8 sequence in JSON string");
			}

			output.append(text_.substr(offset_, sequenceLength));
			offset_ += sequenceLength;
		}

		return fail(
			JsonParseErrorCode::UnexpectedEnd,
			text_.size(),
			"Unterminated JSON string");
	}

	bool parseStringEscape(std::string& output)
	{
		++offset_;
		if (offset_ == text_.size())
		{
			return fail(
				JsonParseErrorCode::InvalidStringEscape,
				offset_,
				"Unexpected end of JSON string escape");
		}

		const char escape = text_[offset_++];
		switch (escape)
		{
		case '"':
			output.push_back('"');
			return true;
		case '\\':
			output.push_back('\\');
			return true;
		case '/':
			output.push_back('/');
			return true;
		case 'b':
			output.push_back('\b');
			return true;
		case 'f':
			output.push_back('\f');
			return true;
		case 'n':
			output.push_back('\n');
			return true;
		case 'r':
			output.push_back('\r');
			return true;
		case 't':
			output.push_back('\t');
			return true;
		case 'u':
			return parseUnicodeEscape(output);
		default:
			return failSyntax(
				JsonParseErrorCode::InvalidStringEscape,
				offset_ - 1,
				"Invalid JSON string escape");
		}
	}

	bool readUnicodeCodeUnit(
		std::uint32_t& codeUnit,
		std::size_t& digitOffset)
	{
		digitOffset = offset_;
		codeUnit = 0;

		for (std::size_t index = 0; index < 4; ++index)
		{
			if (offset_ == text_.size())
			{
				return fail(
					JsonParseErrorCode::InvalidUnicodeEscape,
					offset_,
					"Incomplete JSON Unicode escape");
			}

			const int digit = hexadecimalValue(text_[offset_]);
			if (digit < 0)
			{
				return failSyntax(
					JsonParseErrorCode::InvalidUnicodeEscape,
					offset_,
					"Invalid hexadecimal digit in JSON Unicode escape");
			}

			codeUnit = (codeUnit << 4) | static_cast<std::uint32_t>(digit);
			++offset_;
		}

		return true;
	}

	bool parseUnicodeEscape(std::string& output)
	{
		std::uint32_t firstCodeUnit = 0;
		std::size_t firstDigitOffset = 0;
		if (!readUnicodeCodeUnit(firstCodeUnit, firstDigitOffset))
		{
			return false;
		}

		if (firstCodeUnit >= 0xDC00 && firstCodeUnit <= 0xDFFF)
		{
			return fail(
				JsonParseErrorCode::InvalidUnicodeEscape,
				firstDigitOffset,
				"Unexpected low surrogate in JSON Unicode escape");
		}

		if (firstCodeUnit < 0xD800 || firstCodeUnit > 0xDBFF)
		{
			appendUtf8(output, firstCodeUnit);
			return true;
		}

		if (offset_ == text_.size())
		{
			return fail(
				JsonParseErrorCode::InvalidUnicodeEscape,
				offset_,
				"High surrogate is not followed by a low surrogate");
		}

		if (text_[offset_] != '\\')
		{
			return failSyntax(
				JsonParseErrorCode::InvalidUnicodeEscape,
				offset_,
				"High surrogate is not followed by a Unicode escape");
		}
		++offset_;

		if (offset_ == text_.size())
		{
			return fail(
				JsonParseErrorCode::InvalidUnicodeEscape,
				offset_,
				"High surrogate is followed by an incomplete escape");
		}

		if (text_[offset_] != 'u')
		{
			return failSyntax(
				JsonParseErrorCode::InvalidUnicodeEscape,
				offset_,
				"High surrogate is not followed by a low-surrogate escape");
		}
		++offset_;

		std::uint32_t secondCodeUnit = 0;
		std::size_t secondDigitOffset = 0;
		if (!readUnicodeCodeUnit(secondCodeUnit, secondDigitOffset))
		{
			return false;
		}

		if (secondCodeUnit < 0xDC00 || secondCodeUnit > 0xDFFF)
		{
			return fail(
				JsonParseErrorCode::InvalidUnicodeEscape,
				secondDigitOffset,
				"High surrogate is not followed by a low surrogate");
		}

		const std::uint32_t codePoint =
			0x10000
			+ ((firstCodeUnit - 0xD800) << 10)
			+ (secondCodeUnit - 0xDC00);
		appendUtf8(output, codePoint);
		return true;
	}

	bool parseArray(Json& output, std::size_t containerDepth)
	{
		if (containerDepth >= options_.maximumDepth)
		{
			return fail(
				JsonParseErrorCode::MaximumDepthExceeded,
				offset_,
				"Maximum JSON container depth exceeded");
		}

		++offset_;
		skipWhitespace();

		Json::Array array;
		if (offset_ == text_.size())
		{
			return fail(
				JsonParseErrorCode::UnexpectedEnd,
				offset_,
				"Unterminated JSON array");
		}

		if (text_[offset_] == ']')
		{
			++offset_;
			output = Json(std::move(array));
			return true;
		}

		while (true)
		{
			Json element;
			if (!parseValue(element, containerDepth + 1))
			{
				return false;
			}
			array.push_back(std::move(element));

			skipWhitespace();
			if (offset_ == text_.size())
			{
				return fail(
					JsonParseErrorCode::UnexpectedEnd,
					offset_,
					"Unterminated JSON array");
			}

			if (text_[offset_] == ']')
			{
				++offset_;
				output = Json(std::move(array));
				return true;
			}

			if (text_[offset_] != ',')
			{
				return failSyntax(
					JsonParseErrorCode::ExpectedCommaOrEnd,
					offset_,
					"Expected ',' or ']' in JSON array");
			}

			++offset_;
			skipWhitespace();
			if (offset_ == text_.size())
			{
				return fail(
					JsonParseErrorCode::UnexpectedEnd,
					offset_,
					"Unterminated JSON array");
			}

			if (text_[offset_] == ']')
			{
				return fail(
					JsonParseErrorCode::TrailingComma,
					offset_,
					"Trailing comma in JSON array");
			}
		}
	}

	bool parseObject(Json& output, std::size_t containerDepth)
	{
		if (containerDepth >= options_.maximumDepth)
		{
			return fail(
				JsonParseErrorCode::MaximumDepthExceeded,
				offset_,
				"Maximum JSON container depth exceeded");
		}

		++offset_;
		skipWhitespace();

		Json::Object object;
		if (offset_ == text_.size())
		{
			return fail(
				JsonParseErrorCode::UnexpectedEnd,
				offset_,
				"Unterminated JSON object");
		}

		if (text_[offset_] == '}')
		{
			++offset_;
			output = Json(std::move(object));
			return true;
		}

		while (true)
		{
			if (offset_ == text_.size())
			{
				return fail(
					JsonParseErrorCode::UnexpectedEnd,
					offset_,
					"Expected an object member name");
			}

			if (text_[offset_] != '"')
			{
				return failSyntax(
					JsonParseErrorCode::UnexpectedToken,
					offset_,
					"Expected a quoted object member name");
			}

			const std::size_t keyOffset = offset_;
			std::string key;
			if (!parseString(key))
			{
				return false;
			}

			const bool duplicate = object.contains(key);

			skipWhitespace();
			if (offset_ == text_.size())
			{
				return fail(
					JsonParseErrorCode::UnexpectedEnd,
					offset_,
					"Expected ':' after the object member name");
			}

			if (text_[offset_] != ':')
			{
				return failSyntax(
					JsonParseErrorCode::ExpectedColon,
					offset_,
					"Expected ':' after the object member name");
			}

			++offset_;
			skipWhitespace();

			Json member;
			if (!parseValue(member, containerDepth + 1))
			{
				return false;
			}

			if (duplicate && options_.rejectDuplicateObjectKeys)
			{
				return fail(
					JsonParseErrorCode::DuplicateObjectKey,
					keyOffset,
					"Duplicate JSON object member name");
			}

			object.insert_or_assign(std::move(key), std::move(member));

			skipWhitespace();
			if (offset_ == text_.size())
			{
				return fail(
					JsonParseErrorCode::UnexpectedEnd,
					offset_,
					"Unterminated JSON object");
			}

			if (text_[offset_] == '}')
			{
				++offset_;
				output = Json(std::move(object));
				return true;
			}

			if (text_[offset_] != ',')
			{
				return failSyntax(
					JsonParseErrorCode::ExpectedCommaOrEnd,
					offset_,
					"Expected ',' or '}' in JSON object");
			}

			++offset_;
			skipWhitespace();
			if (offset_ == text_.size())
			{
				return fail(
					JsonParseErrorCode::UnexpectedEnd,
					offset_,
					"Unterminated JSON object");
			}

			if (text_[offset_] == '}')
			{
				return fail(
					JsonParseErrorCode::TrailingComma,
					offset_,
					"Trailing comma in JSON object");
			}
		}
	}

	std::string_view text_;
	const JsonParseOptions& options_;
	std::size_t offset_ = 0;
	std::optional<JsonParseError> error_;
};

std::string appendJsonPointerToken(
	const std::string& pointer,
	std::string_view token)
{
	std::string result = pointer;
	result.push_back('/');

	for (const char character : token)
	{
		if (character == '~')
		{
			result += "~0";
		}
		else if (character == '/')
		{
			result += "~1";
		}
		else
		{
			result.push_back(character);
		}
	}

	return result;
}

class JsonSerializer
{
public:
	JsonSerializeResult serialize(const Json& value)
	{
		JsonSerializeResult result;
		if (!writeValue(value, std::string()))
		{
			result.error = error_;
			return result;
		}

		result.text = std::move(output_);
		return result;
	}

private:
	bool fail(
		JsonSerializeErrorCode code,
		std::string pointer,
		std::string message)
	{
		if (!error_.has_value())
		{
			error_ = JsonSerializeError{
				code,
				std::move(pointer),
				std::move(message)
			};
		}
		return false;
	}

	bool writeValue(const Json& value, const std::string& pointer)
	{
		switch (value.type())
		{
		case JsonType::Null:
			output_ += "null";
			return true;
		case JsonType::Boolean:
			output_ += value.asBoolean() ? "true" : "false";
			return true;
		case JsonType::Number:
			return writeNumber(value.asNumber(), pointer);
		case JsonType::String:
			return writeString(
				value.asString(),
				pointer,
				"JSON string contains malformed UTF-8");
		case JsonType::Array:
			return writeArray(value.asArray(), pointer);
		case JsonType::Object:
			return writeObject(value.asObject(), pointer);
		default:
			return false;
		}
	}

	bool writeNumber(double value, const std::string& pointer)
	{
		if (!std::isfinite(value))
		{
			return fail(
				JsonSerializeErrorCode::NonFiniteNumber,
				pointer,
				"JSON cannot represent a non-finite number");
		}

		if (value == 0.0 && std::signbit(value))
		{
			output_ += "-0";
			return true;
		}

		std::array<char, 128> buffer{};
		const auto conversion = std::to_chars(
			buffer.data(),
			buffer.data() + buffer.size(),
			value);

		if (conversion.ec != std::errc())
		{
			return fail(
				JsonSerializeErrorCode::NonFiniteNumber,
				pointer,
				"Double could not be converted to JSON text");
		}

		output_.append(buffer.data(), conversion.ptr);
		return true;
	}

	bool writeString(
		std::string_view value,
		const std::string& pointer,
		const char* invalidUtf8Message)
	{
		std::size_t failureOffset = 0;
		if (!validateUtf8(value, failureOffset))
		{
			return fail(
				JsonSerializeErrorCode::InvalidUtf8,
				pointer,
				invalidUtf8Message);
		}

		static constexpr char hexadecimalDigits[] = "0123456789ABCDEF";

		output_.push_back('"');
		for (const unsigned char character : value)
		{
			switch (character)
			{
			case '"':
				output_ += "\\\"";
				break;
			case '\\':
				output_ += "\\\\";
				break;
			case '\b':
				output_ += "\\b";
				break;
			case '\f':
				output_ += "\\f";
				break;
			case '\n':
				output_ += "\\n";
				break;
			case '\r':
				output_ += "\\r";
				break;
			case '\t':
				output_ += "\\t";
				break;
			default:
				if (character < 0x20)
				{
					output_ += "\\u00";
					output_.push_back(hexadecimalDigits[character >> 4]);
					output_.push_back(hexadecimalDigits[character & 0x0F]);
				}
				else
				{
					output_.push_back(static_cast<char>(character));
				}
				break;
			}
		}
		output_.push_back('"');
		return true;
	}

	bool writeArray(
		const Json::Array& array,
		const std::string& pointer)
	{
		output_.push_back('[');
		for (std::size_t index = 0; index < array.size(); ++index)
		{
			if (index != 0)
			{
				output_.push_back(',');
			}

			const std::string childPointer =
				pointer + "/" + std::to_string(index);
			if (!writeValue(array[index], childPointer))
			{
				return false;
			}
		}
		output_.push_back(']');
		return true;
	}

	bool writeObject(
		const Json::Object& object,
		const std::string& pointer)
	{
		output_.push_back('{');
		bool first = true;

		for (const auto& member : object)
		{
			if (!first)
			{
				output_.push_back(',');
			}
			first = false;

			// An invalid key cannot itself be represented by a valid Unicode JSON Pointer token.
			if (!writeString(
				member.first,
				pointer,
				"JSON object member name contains malformed UTF-8"))
			{
				return false;
			}

			output_.push_back(':');
			const std::string childPointer =
				appendJsonPointerToken(pointer, member.first);
			if (!writeValue(member.second, childPointer))
			{
				return false;
			}
		}

		output_.push_back('}');
		return true;
	}

	std::string output_;
	std::optional<JsonSerializeError> error_;
};

}

JsonParseResult parseJson(
	std::string_view text,
	const JsonParseOptions& options)
{
	JsonParser parser(text, options);
	return parser.parse();
}

JsonSerializeResult serializeJson(const Json& value)
{
	JsonSerializer serializer;
	return serializer.serialize(value);
}

}
