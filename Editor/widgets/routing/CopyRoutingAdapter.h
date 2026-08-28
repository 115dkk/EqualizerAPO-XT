/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	CopyRoutingAdapter is the single, skin-independent place that converts a
	Copy command's parameter string to/from the engine's std::vector<Assignment>
	(filters/CopyFilter.h) and derives a display-oriented crosspoint matrix view
	of the same data. Every skin's IRoutingRenderer consumes these structures so
	the routing data lives in exactly one form and only the presentation differs.
*/

#pragma once

#include <vector>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

#include "filters/CopyFilter.h"

class CopyRoutingAdapter
{
public:
	// Parse "L=R VSL=0.866*L+-0.5*R" into assignments. Mirrors the engine's
	// CopyFilterFactory::createFilter parsing so the editor and runtime agree.
	static std::vector<Assignment> parse(const QString& parameters);

	// Serialise assignments back to a parameter string. Mirrors
	// CopyFilterGUI::store so a parse/serialise round-trip is lossless.
	static QString serialize(const std::vector<Assignment>& assignments);

	// Shared factor editor grammar: INV, numeric, or numeric dB.
	static bool parseFactorToken(const QString& token, Assignment::Summand& summand);
	static void ensureTargetChannel(std::vector<Assignment>& assignments, QStringList& pinnedChannels,
		const QString& channel);
	static void pinChannel(QStringList& pinnedChannels, const QString& channel);

	// True for channels that are not part of the standard physical layout
	// (the upmix scratch channels such as VSL/VRR). Used to style them as
	// dashed "virtual" badges.
	static bool isVirtualChannel(const QString& channel);

	// Fixed display colour for a channel (physical channels have a stable hue;
	// virtual channels reuse their base colour or a neutral slate).
	static QString channelColor(const QString& channel);

	// ── Crosspoint matrix view ─────────────────────────────────────────────
	// A single source contribution to one output channel.
	struct Cell
	{
		double factor = 1.0;
		bool isDecibel = false;
		bool present = false;
	};

	// Derived grid: outputs are the assignment targets in file order; inputs
	// are every distinct source channel in first-seen order. cell(out,in) holds
	// the coefficient if that routing exists.
	struct Matrix
	{
		QStringList outputs;
		QStringList inputs;

		Cell cell(int outRow, int inCol) const;

		// outRow * inputs.size() + inCol -> Cell
		QHash<int, Cell> cells;
		int indexOf(int outRow, int inCol) const { return outRow * inputs.size() + inCol; }
	};

	static Matrix buildMatrix(const std::vector<Assignment>& assignments);

	// ── Device channel seeding ─────────────────────────────────────────────
	// The views derive their rows/columns from the assignments alone, so an
	// emptied Copy would leave nothing to click and could never be refilled
	// from the GUI. These helpers blend the device channel layout in: every
	// device channel becomes available as an output row / input column even
	// when the command line does not (yet) reference it. Rows whose source sum
	// stays empty are skipped by the serializer, so seeding never changes the
	// written config line.

	// Returns assignments extended with an empty assignment per device channel
	// that is not already a target (original order preserved, seeds appended).
	static std::vector<Assignment> seedTargets(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames);

	// buildMatrix variant that additionally offers every device channel as an
	// input column. The extra columns must be known before the cells are keyed
	// (indexOf depends on the final column count), hence an overload rather
	// than a post-hoc extension.
	static Matrix buildMatrix(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames);

	// buildMatrix variant for a fixed source-port list (RoutingPortModel):
	// the input columns are exactly fixedSources, in order, and summands whose
	// channel is not in the list get no cell. Used by the matrix-shaped views
	// when they render MultiConvolution's IR-channel sources.
	static Matrix buildMatrix(const std::vector<Assignment>& assignments,
		const QStringList& fixedSources);
};
