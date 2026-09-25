/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The generation fence between the analysis worker and its requests: a
	result is published only while the request it was computed for is still
	the newest one. It used to be three hand-written comparisons inside
	AnalysisThread::run(), and the test pinned only the one-line equality, so
	deleting any of the three stayed green (audit #348 F6/TD-37). Every
	publish point now goes through publishIf().

	Qt-free. The fence does no locking of its own: the caller serializes
	begin() and publishIf() with its result mutex, as AnalysisThread does.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

class AnalysisRequestFence
{
public:
	using Ticket = std::uint64_t;

	// A ticket for a new request; every ticket handed out earlier is
	// superseded from now on.
	Ticket begin();

	bool isCurrent(Ticket ticket) const;

	// Runs action only while ticket is current, and answers whether it ran.
	template <typename Action>
	bool publishIf(Ticket ticket, Action&& action)
	{
		if (!isCurrent(ticket))
			return false;

		std::forward<Action>(action)();
		return true;
	}

private:
	std::atomic<Ticket> generation{0};
};
