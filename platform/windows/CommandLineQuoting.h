/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Building a command line that CommandLineToArgvW (and the CRT's argv
	parser, which follows the same rules) splits back into the same
	arguments (audit #348 TD-53).

	The rules for everything after the program name: an argument without a
	space, a tab or a quote passes through unchanged. Otherwise it is wrapped
	in quotes; inside, a run of backslashes is taken literally unless a quote
	follows it, in which case the run is halved and the quote is either
	escaped (odd count) or closes the quoted part (even count). So a run in
	front of a quote, or at the end of the argument where the closing quote
	follows, is doubled, and a quote gets one more backslash of its own.

	The two builders this replaces quoted on a space but left the
	backslashes alone, so "C:\Dir\" came back as C:\Dir" and every argument
	after it changed.

	The program name (argv[0]) follows different rules and is not built here.
*/

#pragma once

#include <string>
#include <vector>

namespace winutil
{

inline std::wstring quoteCommandLineArgument(const std::wstring& argument)
{
	if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
		return argument;

	std::wstring quoted(1, L'"');
	for (size_t i = 0; ; i++)
	{
		size_t backslashes = 0;
		while (i < argument.size() && argument[i] == L'\\')
		{
			backslashes++;
			i++;
		}

		if (i == argument.size())
		{
			// The closing quote follows the run.
			quoted.append(backslashes * 2, L'\\');
			break;
		}
		if (argument[i] == L'"')
		{
			quoted.append(backslashes * 2 + 1, L'\\');
			quoted.push_back(L'"');
		}
		else
		{
			quoted.append(backslashes, L'\\');
			quoted.push_back(argument[i]);
		}
	}
	quoted.push_back(L'"');
	return quoted;
}

// The arguments quoted and joined by single spaces, for the part of a
// command line after the program name (ShellExecuteEx's lpParameters, or
// what follows a quoted executable path).
inline std::wstring joinCommandLineArguments(const std::vector<std::wstring>& arguments)
{
	std::wstring line;
	for (const std::wstring& argument : arguments)
	{
		if (!line.empty())
			line.push_back(L' ');
		line += quoteCommandLineArgument(argument);
	}
	return line;
}

}
