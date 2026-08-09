/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "text/WideString.h"
#include "parser/NumericText.h"

#include <cwctype>

#include "MultiConvolutionCommand.h"


namespace
{
// A word ends at whitespace, '=' or '+', so "L=0+1" and "L = 0 + 1" tokenize
// identically. Words that turn out not to fit the mapping grammar are handed
// back by position (the raw remainder becomes the path), so a path containing
// '=' or '+' is never cut apart.
std::wstring readWord(const std::wstring& text, size_t& pos)
{
	const size_t start = pos;
	while (pos < text.size() && !iswspace(text[pos]) && text[pos] != L'=' && text[pos] != L'+')
		pos++;
	return text.substr(start, pos - start);
}

void skipSpace(const std::wstring& text, size_t& pos)
{
	while (pos < text.size() && iswspace(text[pos]))
		pos++;
}

// An IR channel reference is a plain 0-based decimal number. Four digits bound
// the value well above libsndfile's channel limit while keeping wcstoul safe.
bool parseIrChannel(const std::wstring& word, unsigned& value)
{
	if (word.empty() || word.size() > 4)
		return false;
	for (wchar_t c : word)
		if (c < L'0' || c > L'9')
			return false;
	value = (unsigned)wcstoul(word.c_str(), nullptr, 10);
	return true;
}

// A summand is "<ir ch>" or "<factor>*<ir ch>" (Copy's factor grammar: decimal
// number, optional dB suffix). '*' is not a word delimiter, so the whole
// summand arrives as one word; a word that fits neither form is rejected and
// the caller rewinds it into the path.
bool parseSummand(const std::wstring& word, MultiConvolutionCommand::IrChannelRef& ref)
{
	const size_t star = word.find(L'*');
	if (star == std::wstring::npos)
	{
		unsigned channel = 0;
		if (!parseIrChannel(word, channel))
			return false;
		ref = MultiConvolutionCommand::IrChannelRef(channel);
		return true;
	}

	if (star == 0 || star + 1 >= word.size() || word.find(L'*', star + 1) != std::wstring::npos)
		return false;

	unsigned channel = 0;
	if (!parseIrChannel(word.substr(star + 1), channel))
		return false;

	std::wstring factorText = word.substr(0, star);
	bool isDecibel = false;
	if (factorText.size() > 2 && text::toLower(factorText.substr(factorText.size() - 2)) == L"db")
	{
		isDecibel = true;
		factorText.resize(factorText.size() - 2);
	}

	// Audit #250 F015: accept the decimal comma like the BiQuad family.
	factorText = numeric_text::normalizeDecimalComma(factorText);

	// The factor must be a complete number: a partial parse means the word was
	// not a summand (e.g. a stray file name), not a factor with trailing junk.
	wchar_t* end = nullptr;
	const double factor = wcstod(factorText.c_str(), &end);
	if (end == factorText.c_str() || *end != L'\0')
		return false;

	ref = MultiConvolutionCommand::IrChannelRef(channel, factor, isDecibel);
	return true;
}
}

bool MultiConvolutionCommand::isSimpleForm() const
{
	return mappings.size() == 1 && mappings[0].irChannels.empty();
}

std::wstring MultiConvolutionCommand::serializeMappingsOnly() const
{
	// The simple form keeps its bare "<target>" spelling. In the mapping form
	// a mapping whose sum is empty is not representable (re-parsing would read
	// the bare target as the path start), so such placeholder rows are skipped,
	// matching the Copy serializer's handling of empty assignments.
	const bool simple = isSimpleForm();
	std::wstring result;
	for (const Mapping& mapping : mappings)
	{
		if (mapping.targetChannel.empty())
			continue;
		if (!simple && mapping.irChannels.empty())
			continue;
		if (!result.empty())
			result += L" ";
		result += mapping.targetChannel;
		if (!mapping.irChannels.empty())
		{
			result += L"=";
			bool firstChannel = true;
			for (const IrChannelRef& ref : mapping.irChannels)
			{
				if (!firstChannel)
					result += L"+";
				firstChannel = false;
				if (ref.factor != 1.0 || ref.isDecibel)
				{
					// "%g" matches Copy's factor formatting; the '*' separator
					// makes a bare integer factor unambiguous on re-parse, so
					// no ".0" suffix is needed here.
					wchar_t buffer[64];
					swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%g", ref.factor);
					result += buffer;
					if (ref.isDecibel)
						result += L"dB";
					result += L"*";
				}
				result += std::to_wstring(ref.channel);
			}
		}
	}
	return result;
}

const std::wstring& MultiConvolutionCommand::serialize() const
{
	serialized = serializeMappingsOnly();
	if (!serialized.empty())
		serialized += L" ";
	serialized += path;
	return serialized;
}

bool MultiConvolutionCommand::parse(const std::wstring& command, const std::wstring& parameters, MultiConvolutionCommand& out)
{
	if (command != L"MultiConvolution")
		return false;

	const std::wstring trimmed = text::trim(parameters);

	std::vector<Mapping> mappings;
	size_t pos = 0;
	size_t pathStart = std::wstring::npos;

	while (true)
	{
		skipSpace(trimmed, pos);
		const size_t wordStart = pos;
		const std::wstring word = readWord(trimmed, pos);
		if (word.empty())
			break;

		size_t afterWord = pos;
		skipSpace(trimmed, afterWord);
		if (afterWord >= trimmed.size() || trimmed[afterWord] != L'=')
		{
			// Not a mapping: this word starts the path.
			pathStart = wordStart;
			break;
		}

		// "<word> =": parse the IR channel sum. If it breaks off mid-way, the
		// word was not a target after all (e.g. a file name like "a=b.wav"), so
		// rewind and let the whole word start the path instead.
		pos = afterWord + 1;
		Mapping mapping;
		mapping.targetChannel = word;
		bool valid = true;
		while (true)
		{
			skipSpace(trimmed, pos);
			const std::wstring summand = readWord(trimmed, pos);
			MultiConvolutionCommand::IrChannelRef ref;
			if (!parseSummand(summand, ref))
			{
				valid = false;
				break;
			}
			mapping.irChannels.push_back(ref);

			size_t next = pos;
			skipSpace(trimmed, next);
			if (next < trimmed.size() && trimmed[next] == L'+')
			{
				pos = next + 1;
				continue;
			}
			break;
		}
		if (!valid)
		{
			pathStart = wordStart;
			break;
		}
		mappings.push_back(mapping);
	}

	std::wstring path;
	if (mappings.empty())
	{
		// No leading mapping parsed: fall back to the simple "<target> <path>"
		// form, exactly as the original single-channel grammar read it.
		const size_t space = trimmed.find_first_of(L" \t");
		if (space == std::wstring::npos)
			return false;

		std::wstring target = trimmed.substr(0, space);
		path = text::trim(trimmed.substr(space + 1));
		if (target.empty() || path.empty())
			return false;

		mappings.push_back({std::move(target), {}});
	}
	else
	{
		if (pathStart == std::wstring::npos || pathStart >= trimmed.size())
			return false;
		path = text::trim(trimmed.substr(pathStart));
		if (path.empty())
			return false;
	}

	out.mappings = std::move(mappings);
	out.path = std::move(path);
	return true;
}
