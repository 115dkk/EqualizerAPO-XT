// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace subroute::text
{

struct Utf8Validation
{
	bool valid = true;
	std::size_t validBytes = 0;
	std::size_t failureOffset = 0;
};

Utf8Validation validateUtf8(
	std::string_view value,
	std::size_t maximumSequences = static_cast<std::size_t>(-1)) noexcept;

std::string appendJsonPointer(
	std::string_view parent,
	std::string_view token);

std::string appendJsonPointer(
	std::string_view parent,
	std::size_t index);

}
