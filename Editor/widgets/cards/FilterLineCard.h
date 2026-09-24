/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Which card a "Filter:" line opens in. "Filter" is the one command keyword
	with more than one card behind it, and most Filter lines (an ordinary
	peaking or shelf biquad) open none: they keep the legacy knob GUI. The
	answer comes from the engine's own parsers, so it is widget-free and both
	the registry's predicate and the creator in FilterCardEditorRouter.cpp
	read it (audit #348 TD-57).
*/

#pragma once

class QString;

enum class FilterLineCard
{
	None,             // any other Filter line: the legacy knob GUI
	IirCoefficients,  // "Filter: ON IIR Order n Coefficients ..."
	AllPass           // a biquad of type AP / AP1
};

// IIR first, because its parser is the stricter one and rejects everything
// that is not an explicit coefficient line; then the all-pass types the
// BiQuad parser reports.
FilterLineCard filterLineCard(const QString& command, const QString& parameters);
