/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AnalysisRequestFence.h"

AnalysisRequestFence::Ticket AnalysisRequestFence::begin()
{
	return generation.fetch_add(1, std::memory_order_relaxed) + 1;
}

bool AnalysisRequestFence::isCurrent(Ticket ticket) const
{
	return ticket == generation.load(std::memory_order_relaxed);
}
