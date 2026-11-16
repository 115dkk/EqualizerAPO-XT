/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>

namespace vcruntime
{
using Version = std::array<unsigned long, 4>;
enum class Outcome { NotNeeded, Completed, Running, Failed };
struct ProcessResult
{
	bool started = false;
	std::optional<unsigned long> exitCode;
};
using Runner = std::function<ProcessResult()>;
bool needsInstall(const std::optional<Version>& installed, const std::optional<Version>& shipped);
Outcome runIfNeeded(const std::optional<Version>& installed, const std::optional<Version>& shipped,
	const Runner& runner);
// Best effort only. Never runs an installer in tests: the runner above is injected.
void ensureInstalled(const std::wstring& installDir);
}
