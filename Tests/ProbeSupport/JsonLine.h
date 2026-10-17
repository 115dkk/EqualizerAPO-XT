/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Small buffered writer for the probes' one-object-per-line JSON output.
*/

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace probesupport
{
	inline std::string jsonEscape(const std::wstring& text)
	{
		if (text.empty())
			return {};
		const int byteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
			static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		if (byteCount <= 0)
			return "?";
		std::string utf8(static_cast<size_t>(byteCount), '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
			utf8.data(), byteCount, nullptr, nullptr);
		std::string escaped;
		for (unsigned char character : utf8)
		{
			switch (character)
			{
			case '"': escaped += "\\\""; break;
			case '\\': escaped += "\\\\"; break;
			case '\b': escaped += "\\b"; break;
			case '\f': escaped += "\\f"; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			case '\t': escaped += "\\t"; break;
			default:
				if (character < 0x20)
				{
					char encoded[7] = {};
					sprintf_s(encoded, "\\u%04X", static_cast<unsigned>(character));
					escaped += encoded;
				}
				else
				{
					escaped += static_cast<char>(character);
				}
			}
		}
		return escaped;
	}

	class JsonLine
	{
	public:
		void append(const char* format, ...)
		{
			va_list arguments;
			va_start(arguments, format);
			va_list measured;
			va_copy(measured, arguments);
			const int characters = std::vsnprintf(nullptr, 0, format, measured);
			va_end(measured);
			if (characters > 0)
			{
				const size_t offset = text_.size();
				text_.resize(offset + static_cast<size_t>(characters) + 1);
				std::vsnprintf(text_.data() + offset, static_cast<size_t>(characters) + 1, format, arguments);
				text_.resize(offset + static_cast<size_t>(characters));
			}
			va_end(arguments);
		}

		void write(FILE* stream = stdout) const
		{
			std::fwrite(text_.data(), 1, text_.size(), stream);
			std::fputc('\n', stream);
		}

	private:
		std::string text_;
	};
}
