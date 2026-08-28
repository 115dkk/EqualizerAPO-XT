/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	MultiConvolutionRoutingAdapter is the single place that converts a
	MultiConvolution command's mappings to/from the routing views' working type
	(std::vector<Assignment>, filters/CopyFilter.h). The views stay Copy-shaped;
	an impulse-response channel rides as a decimal summand channel ("0", "1",
	...) carrying its factor, and the fixed source-port list comes from the
	file's channel count. The card supplies these structures to every skin's
	IRoutingRenderer, so MultiConvolution routing gets the same per-skin
	presentations as Copy, including factor editing.
*/

#pragma once

#include <vector>
#include <QString>
#include <QStringList>

#include "filters/CopyFilter.h"
#include "filters/MultiConvolutionCommand.h"

class MultiConvolutionRoutingAdapter
{
public:
	// Mappings -> the views' Assignment type. IR channel k becomes a summand
	// with channel "k" and its factor. A simple-form mapping (empty list)
	// expands to every file channel 0..fileChannelCount-1 for display; when the
	// file's channel count is unknown (<= 0) it expands to nothing, so callers
	// must not offer editing in that state (an edit would persist the empty
	// expansion and drop the row from the line).
	static std::vector<Assignment> toAssignments(const std::vector<MultiConvolutionCommand::Mapping>& mappings,
		int fileChannelCount);

	// The views' edited assignments -> mappings. Summands whose channel is not
	// a plain decimal number are dropped (the fixed source ports are the only
	// offered sources, so these only appear through hand-edited lines), and
	// assignments whose sum ends up empty are skipped like Copy's serializer
	// skips them (seeded placeholder rows).
	static std::vector<MultiConvolutionCommand::Mapping> toMappings(const std::vector<Assignment>& assignments);

	// The fixed source-port labels for the routing view: "0".."N-1" from the
	// file, followed by any referenced index beyond the file's channel count
	// (so a stale connection stays visible and removable).
	static QStringList sourcePorts(int fileChannelCount,
		const std::vector<MultiConvolutionCommand::Mapping>& mappings);
};
