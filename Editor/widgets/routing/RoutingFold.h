/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	RoutingFold is the shared, presentation-free half of the Copy and
	MultiConvolution routing views' target-channel fold. Seeding every device
	channel keeps an emptied command editable, but laying the whole seeded set
	out flat made the views grow with the device (a 7.1 layout is an 8-row
	matrix of mostly empty cells, and most skins draw the routing as exactly
	such a matrix). The fold keeps the seeded surface and collapses its
	presentation: a collapsed view shows only the channels the command actually
	involves (or two representatives when the command is empty), and everything
	else waits behind the per-skin reveal control. Serialization is untouched -
	it never wrote empty rows.

	This TU is Qt Core + the Assignment struct only (no parser, no engine),
	so EditorLogicTests compiles it directly.
*/

#pragma once

#include <vector>
#include <QString>
#include <QStringList>
#include <QVector>

#include "filters/CopyFilter.h"

namespace RoutingFold
{
struct Fold
{
	// Indices into the seeded assignments, in seeded order.
	QVector<int> visibleRows;

	// Visible input columns for the matrix-shaped views, in stable order:
	// first-seen across the visible rows' sums, then pinned channels, then
	// (expanded) the remaining device channels or (collapsed, nothing
	// referenced) the representative device channels.
	QStringList inputs;

	// Rows folded away; drives the reveal control's "+N" label.
	int hiddenChannels = 0;
};

// Partition the seeded assignments into visible and folded rows. pinned
// holds channels that stay visible even while their sum is empty: the
// targets the command referenced when the view was created, plus every
// channel the user added by name this session. When nothing is referenced
// or pinned, the first two device channels stand in as representatives so
// an empty command still offers something to click. fixedInputs locks the
// source side to an external port list (the selected MultiConvolution WAV);
// only target rows fold in that mode.
Fold fold(const std::vector<Assignment>& seeded,
	const std::vector<std::wstring>& channelNames,
	const QStringList& pinned, bool expanded,
	const QStringList& fixedInputs = QStringList());

// The targets that arrive with a non-empty sum - the initial pin set (these
// rows must never fold away mid-session just because their last source was
// removed).
QStringList referencedTargets(const std::vector<Assignment>& assignments);

// Validation for names typed into the add-channel editors. Channel names
// live inside the Copy grammar ("VC=0.5*L+0.5*R"), so anything the parser
// would read as an operator or a factor is rejected: only [A-Za-z0-9_-],
// at least one letter, at most 16 characters.
bool isValidChannelName(const QString& name);

// Remove the channel as a target row and as a summand in every remaining
// sum (case-insensitive). Returns true when a summand or a non-empty row
// was dropped - i.e. the serialized line changes; folding away a pure seed
// row returns false.
bool removeChannel(std::vector<Assignment>& assignments, const QString& channel);

// The factor grammar of every renderer's inline gain editor: "INV" (phase
// inversion, -1), a decimal number, or a decimal number with a dB suffix.
// A decimal comma is accepted like the engine accepts it. Only the factor
// fields of summand change; false leaves it untouched.
bool parseFactor(const QString& token, Assignment::Summand& summand);

// The source-token grammar of the step list's inline source editor: the
// summand exactly as the line writes it, so what the editor shows is what
// the config line will say. "0.5*L" and "-6dB*L" set factor and channel;
// a bare factor ("0.5", "-6dB", "INV") changes only the factor and keeps
// the channel; a bare channel token sets the channel at unity, which is
// what the line grammar reads a bare token as. In fixed-port mode
// (MultiConvolution, ports "0".."N-1") a bare integer is a port index, the
// one thing a bare integer cannot be there is a factor; in Copy mode "0"
// and decimals are factors (the Copy grammar) and every other token is a
// channel name or a 1-based position. False when the token is not a
// summand at all; summand is untouched then.
bool parseSourceToken(const QString& token, bool fixedPorts, Assignment::Summand& summand);

// The token the source editor pre-fills: the summand as the line writes it
// ("L", "0.5*L", "-6dB*L", "-1.0*L"), formatted like the Copy serializer.
QString sourceToken(const Assignment::Summand& summand);
}
