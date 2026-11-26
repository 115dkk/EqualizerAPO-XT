/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The summary a SubwooferRouting line shows on its reference card.
*/

#pragma once

#include <QString>

// SubwooferRoutingCardEditor reads the routing state into this summary and
// turns it into the reference card's state (ReferenceCardState).
//
// The card speaks the user's language: layout, crossover corner, source-LFE
// handling, headroom and profile. Internal graph statistics (group, path and
// matrix-edge counts) belong to the full editor, not the summary card.
//
// Status contract (review round 2): errorText is the single error sentence
// and warningText the single advisory sentence. The editor guarantees that
// valid == false implies a non-empty errorText, and that a missing linked
// profile is already described by errorText. Views must render each text at
// most once and must not synthesize additional error lines from `valid` or
// `profileMissing` - those flags only drive badge tone and short badges.
struct SubwooferRoutingCardState
{
	bool enabled = true;
	bool valid = false;
	QString layoutLabel;
	bool sourceLfePreserved = false;
	double sourceLfeGainDb = 0.0;
	// Representative crossover corners; 0.0 when the state has no section of
	// that type (a full-range layout). The slope labels carry the recognized
	// alignment ("BW2", "LR4"); empty for custom chains.
	double highPassHz = 0.0;
	double lowPassHz = 0.0;
	QString highPassSlope;
	QString lowPassSlope;
	bool headroomAuto = true;
	double headroomTrimDb = 0.0;
	QString profileName;
	bool linkedProfile = false;
	bool profileMissing = false;
	QString warningText;
	QString errorText;
};

namespace SubwooferRoutingCardText
{
// The crossover corners in words: one frequency when the high and low pass
// share it, otherwise "HP ... / LP ...", and "Full range" with neither.
QString crossoverSummary(const SubwooferRoutingCardState& state);
}
