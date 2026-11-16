/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <algorithm>
#include <windows.h>

// Hook-only budget: regular Device Selector operations keep their timeouts.
// The elevated child's absolute deadline includes service waits and ACL helper
// processes. UAC consent is outside it (audit #467 TD-64). Nested operations
// do not get a fresh allowance.
namespace installwait
{
inline thread_local ULONGLONG deadline = 0;

inline ULONGLONG hookDeadline(bool elevated, ULONGLONG previous, ULONGLONG now, DWORD milliseconds)
{
	if (!elevated)
		return previous;
	const auto requested = now + milliseconds;
	return previous == 0 ? requested : (std::min)(previous, requested);
}

class Budget
{
public:
	explicit Budget(DWORD milliseconds, bool elevated = true) : previous(deadline)
	{
		deadline = hookDeadline(elevated, previous, GetTickCount64(), milliseconds);
	}
	~Budget() {deadline = previous;}
	Budget(const Budget&) = delete;
	Budget& operator=(const Budget&) = delete;
private:
	ULONGLONG previous;
};

inline DWORD remaining(DWORD requested)
{
	if (deadline == 0)
		return requested;
	const auto now = GetTickCount64();
	return now >= deadline ? 0 : static_cast<DWORD>((std::min)(deadline - now, ULONGLONG(requested)));
}
}
