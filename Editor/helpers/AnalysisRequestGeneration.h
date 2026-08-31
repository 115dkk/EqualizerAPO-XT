/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>

inline bool isCurrentAnalysisRequest(uint64_t snapshotGeneration, uint64_t currentGeneration)
{
	return snapshotGeneration == currentGeneration;
}
