/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	RoutingFold is the shared, presentation-free half of the Copy routing
	views' channel fold. Seeding every device channel keeps an emptied Copy
	editable, but laying the whole seeded set out flat made the views grow
	with the device (a 7.1 layout is an 8-row matrix of mostly empty cells,
	and most skins draw the routing as exactly such a matrix). The fold keeps
	the seeded surface and collapses its presentation: a collapsed view shows
	only the channels the command actually involves (or two representatives
	when the command is empty), and everything else waits behind the per-skin
	reveal control. Serialization is untouched - it never wrote empty rows.

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
// an empty Copy still offers something to click.
Fold fold(const std::vector<Assignment>& seeded,
	const std::vector<std::wstring>& channelNames,
	const QStringList& pinned, bool expanded);

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
}
