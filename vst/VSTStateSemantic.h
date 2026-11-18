/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace vststate
{
using WritableParameterValues = std::vector<std::pair<uint32_t, double>>;

inline bool shouldStore(bool rawStateChanged, bool semanticStateAvailable,
	const WritableParameterValues& committedParameters,
	const WritableParameterValues& currentParameters,
	uint64_t committedEditSequence, uint64_t currentEditSequence,
	bool forceRawState)
{
	if (forceRawState)
		return rawStateChanged || (semanticStateAvailable && currentParameters != committedParameters);
	if (!semanticStateAvailable)
		return rawStateChanged;
	if (currentParameters != committedParameters)
		return true;
	return rawStateChanged && currentEditSequence != committedEditSequence;
}
}
