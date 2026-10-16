/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <algorithm>
#include <windows.h>

// Hook-only budget: regular Device Selector operations keep their timeouts.
// A single absolute deadline includes service waits, ACL helper processes and
// the elevated child wait. Nested operations do not get a fresh allowance.
namespace installwait
{
inline thread_local ULONGLONG deadline = 0;

class Budget
{
public:
	explicit Budget(DWORD milliseconds) : previous(deadline)
	{
		const auto requested = GetTickCount64() + milliseconds;
		deadline = previous == 0 ? requested : (std::min)(previous, requested);
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
