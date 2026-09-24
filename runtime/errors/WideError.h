/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The common base of the exceptions that carry a wide-character message
	(RegistryError, DeviceException, WindowsServiceError,
	AccessQueryException, ReceiveException, VoicemeeterClient's InitError).
	They had no common base and did not derive from std::exception, so a
	caller could not catch "any of them" and each call site picked its own
	combination of catch clauses, some of which missed a type (audit #348
	C1/TD-32). Catch `const WideError&` where the kind does not matter.

	Header-only on purpose: the ASIO wrapper DLL and the test probes throw
	these without linking Common.lib, and the UTF-8 form for what() needs no
	Windows header.
*/

#pragma once

#include <cstdint>
#include <exception>
#include <string>

class WideError : public std::exception
{
public:
	explicit WideError(std::wstring message)
		: message(std::move(message)), narrow(toUtf8(this->message))
	{
	}

	const std::wstring& getMessage() const noexcept
	{
		return message;
	}

	// The message as UTF-8, for code that only knows std::exception.
	const char* what() const noexcept override
	{
		return narrow.c_str();
	}

private:
	// UTF-16 to UTF-8. An unpaired surrogate becomes U+FFFD.
	static std::string toUtf8(const std::wstring& text)
	{
		std::string out;
		out.reserve(text.size());
		for (size_t i = 0; i < text.size(); i++)
		{
			uint32_t codePoint = static_cast<uint16_t>(text[i]);
			if (codePoint >= 0xD800 && codePoint <= 0xDBFF && i + 1 < text.size()
				&& static_cast<uint16_t>(text[i + 1]) >= 0xDC00 && static_cast<uint16_t>(text[i + 1]) <= 0xDFFF)
			{
				codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (static_cast<uint16_t>(text[i + 1]) - 0xDC00);
				i++;
			}
			else if (codePoint >= 0xD800 && codePoint <= 0xDFFF)
			{
				codePoint = 0xFFFD;
			}

			if (codePoint < 0x80)
			{
				out += static_cast<char>(codePoint);
			}
			else if (codePoint < 0x800)
			{
				out += static_cast<char>(0xC0 | (codePoint >> 6));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
			else if (codePoint < 0x10000)
			{
				out += static_cast<char>(0xE0 | (codePoint >> 12));
				out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xF0 | (codePoint >> 18));
				out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
		}
		return out;
	}

	std::wstring message;
	std::string narrow;
};
