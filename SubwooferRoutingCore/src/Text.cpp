// SPDX-License-Identifier: MIT

#include "Text.h"

namespace subroute::text
{

Utf8Validation validateUtf8(
	std::string_view value,
	std::size_t maximumSequences) noexcept
{
	Utf8Validation result;
	std::size_t sequenceCount = 0;

	while (result.validBytes < value.size()
		&& sequenceCount < maximumSequences)
	{
		const std::size_t offset = result.validBytes;
		const auto byteAt = [&value](std::size_t index)
		{
			return static_cast<unsigned char>(value[index]);
		};

		const unsigned char first = byteAt(offset);
		if (first <= 0x7fU)
		{
			++result.validBytes;
			++sequenceCount;
			continue;
		}

		std::size_t requiredLength = 0;
		unsigned char secondMinimum = 0x80U;
		unsigned char secondMaximum = 0xbfU;

		if (first >= 0xc2U && first <= 0xdfU)
		{
			requiredLength = 2;
		}
		else if (first == 0xe0U)
		{
			requiredLength = 3;
			secondMinimum = 0xa0U;
		}
		else if (first >= 0xe1U && first <= 0xecU)
		{
			requiredLength = 3;
		}
		else if (first == 0xedU)
		{
			requiredLength = 3;
			secondMaximum = 0x9fU;
		}
		else if (first >= 0xeeU && first <= 0xefU)
		{
			requiredLength = 3;
		}
		else if (first == 0xf0U)
		{
			requiredLength = 4;
			secondMinimum = 0x90U;
		}
		else if (first >= 0xf1U && first <= 0xf3U)
		{
			requiredLength = 4;
		}
		else if (first == 0xf4U)
		{
			requiredLength = 4;
			secondMaximum = 0x8fU;
		}
		else
		{
			result.valid = false;
			result.failureOffset = offset;
			return result;
		}

		if (offset + 1 >= value.size())
		{
			result.valid = false;
			result.failureOffset = value.size();
			return result;
		}

		const unsigned char second = byteAt(offset + 1);
		if (second < secondMinimum || second > secondMaximum)
		{
			result.valid = false;
			result.failureOffset = offset + 1;
			return result;
		}

		for (std::size_t index = 2; index < requiredLength; ++index)
		{
			if (offset + index >= value.size())
			{
				result.valid = false;
				result.failureOffset = value.size();
				return result;
			}

			const unsigned char continuation = byteAt(offset + index);
			if (continuation < 0x80U || continuation > 0xbfU)
			{
				result.valid = false;
				result.failureOffset = offset + index;
				return result;
			}
		}

		result.validBytes += requiredLength;
		++sequenceCount;
	}

	result.failureOffset = result.validBytes;
	return result;
}

std::string appendJsonPointer(
	std::string_view parent,
	std::string_view token)
{
	std::string pointer(parent);
	pointer.push_back('/');
	for (const char character : token)
	{
		if (character == '~')
			pointer += "~0";
		else if (character == '/')
			pointer += "~1";
		else
			pointer.push_back(character);
	}
	return pointer;
}

std::string appendJsonPointer(
	std::string_view parent,
	std::size_t index)
{
	return appendJsonPointer(parent, std::to_string(index));
}

}
