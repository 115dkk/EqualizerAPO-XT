/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

// What the analysis graph is showing. One analysis run answers all three, so
// switching between them costs no FilterEngine run and no FFT - see
// AnalysisResponse.
//
// Magnitude is the default and the only one that existed before: it is what a
// user expects to open onto, and every stored preference falls back to it.
enum class AnalysisMetric
{
	MagnitudeDb,
	PhaseDegrees,
	GroupDelayMs
};
