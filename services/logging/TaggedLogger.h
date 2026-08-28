/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <cstdarg>
#include <cstdio>

#include "Logging.h"

namespace logging
{
// Formats "[Tag] LEVEL: message" log lines. The install/shell services used to
// carry one private copy of this formatter each (audit #275); construct one
// per file with its tag and the call sites read as before:
//
//     constexpr logging::TaggedLogger logLine(L"ApoRegistration");
//     ...
//     logLine(L"ERR", L"LoadLibrary failed for %s (gle=%lu)", path, gle);
class TaggedLogger
{
public:
	explicit constexpr TaggedLogger(const wchar_t* tag)
		: tag(tag)
	{
	}

	void operator ()(const wchar_t* level, const wchar_t* format, ...) const
	{
		wchar_t buffer[1024];
		va_list args;
		va_start(args, format);
		_vsnwprintf_s(buffer, _TRUNCATE, format, args);
		va_end(args);
		LogFStatic(L"[%s] %s: %s", tag, level, buffer);
	}

private:
	const wchar_t* tag;
};
}
