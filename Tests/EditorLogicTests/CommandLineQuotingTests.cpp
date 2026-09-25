/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	winutil::quoteCommandLineArgument is meant to be the exact inverse of
	CommandLineToArgvW, so the test asks CommandLineToArgvW itself (audit #348
	TD-53). The program name follows other rules, so every line starts with a
	plain one and only what follows it is compared.
*/

#include "EditorLogicTestSupport.h"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "platform/windows/CommandLineQuoting.h"
#include "platform/windows/Win32Resource.h"

namespace
{
std::vector<std::wstring> splitAfterProgramName(const std::wstring& arguments)
{
	std::vector<std::wstring> result;
	const std::wstring line = L"program.exe " + arguments;
	int argc = 0;
	winutil::UniqueLocalPtr<wchar_t*> argv(CommandLineToArgvW(line.c_str(), &argc));
	if (!argv)
		return result;
	for (int i = 1; i < argc; i++)
		result.push_back(argv.get()[i]);
	return result;
}
}

void testCommandLineQuotingRoundTripsThroughCommandLineToArgvW()
{
	const std::vector<std::wstring> cases = {
		L"plain",
		L"a b",
		L"a\"b",
		L"a\\\"b",
		L"a\\",
		L"a\\\\",
		L"\\",
		L"",
		L"a\\\\ b",
		L"a\tb",
		L"C:\\Program Files\\EqualizerAPO-XT\\",
		L"\"quoted\"",
		L"\\\\server\\share\\x y",
		L"--veloapp-install",
		// Hangul, spelled as escapes so the source stays ASCII.
		L"C:\\\uC0AC\uC6A9\uC790\\\uC124\uC815 \uD30C\uC77C.txt",
	};

	for (const std::wstring& argument : cases)
	{
		const std::vector<std::wstring> parsed = splitAfterProgramName(winutil::quoteCommandLineArgument(argument));
		const QString label = QStringLiteral("[") + QString::fromStdWString(argument) + QStringLiteral("]");
		requireEqual(static_cast<int>(parsed.size()), 1, "one argument comes back as one argument: " + label);
		expectEqual(QString::fromStdWString(parsed[0]), QString::fromStdWString(argument),
			"an argument comes back unchanged: " + label);
	}

	// All of them on one line: an argument's quoting must not leak into the next.
	const std::vector<std::wstring> parsed = splitAfterProgramName(winutil::joinCommandLineArguments(cases));
	requireEqual(static_cast<int>(parsed.size()), static_cast<int>(cases.size()),
		QStringLiteral("a joined line splits into as many arguments as went in"));
	for (size_t i = 0; i < cases.size(); i++)
		expectEqual(QString::fromStdWString(parsed[i]), QString::fromStdWString(cases[i]),
			QStringLiteral("a joined line gives each argument back in place"));

	// What passes through untouched, so the common case stays readable.
	expectEqual(QString::fromStdWString(winutil::quoteCommandLineArgument(L"--veloapp-install")),
		QStringLiteral("--veloapp-install"), QStringLiteral("a flag needs no quotes"));
	expectEqual(QString::fromStdWString(winutil::quoteCommandLineArgument(L"C:\\Dir\\")),
		QStringLiteral("C:\\Dir\\"), QStringLiteral("backslashes alone need no quotes"));
}
