/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The minimal skin's designed console inks for channel identity. On the
	terminal ground a solid primary chip is GUI badge vocabulary, so channels
	print as bare colored ink instead. The inks are a designed table, not a
	transform of the shared hue: merely desaturating the shared palette
	leaves three neighbor pairs (RL/LFE 13, SBR/RR 16, SBL/SL 13 degrees)
	indistinguishable at console saturation. The table re-spaces the eight
	primary channels onto ANSI/base16 hue families (min neighbor gap 24
	degrees) and migrates SBL/SBR to genuinely free wheel regions (violet
	300, olive 95). Dark sits between the muted and body inks; light is
	sunken print ink, every value >= 4.3:1 on its ground. Shared by the Copy
	listing and the header channel-scope tokens, so one channel wears one
	ink everywhere. (Constitution: Copy routing; precedent: channel tone
	r1/r2, header badges 2026-08-24.)

	The table is keyed by the canonical channel name ChannelIdentity
	resolves (a virtual VSL prints in SL's ink), not by the identity
	palette's hex values: keyed by hex, any palette edit silently dropped
	the edited channel to the computed fallback below.
*/

#pragma once

#include <QColor>
#include <QHash>
#include <QString>

#include "Editor/widgets/routing/ChannelIdentity.h"

inline QColor minimalChannelInk(const QString& channel, bool dark)
{
	struct Ink { const char* dark; const char* light; };
	static const QHash<QString, Ink> inks = {
		{ QStringLiteral("L"), { "#CC7578", "#972B2E" } },    // red 358
		{ QStringLiteral("R"), { "#809BD0", "#2E539E" } },    // blue 220
		{ QStringLiteral("C"), { "#79C38C", "#2D8042" } },    // green 135
		{ QStringLiteral("LFE"), { "#C8B879", "#8A7728" } },  // yellow 48
		{ QStringLiteral("SUB"), { "#C8B879", "#8A7728" } },  // yellow 48, LFE's ink
		{ QStringLiteral("RL"), { "#C39779", "#8C5531" } },   // orange 24
		{ QStringLiteral("RR"), { "#7DC5CA", "#2E848A" } },   // cyan 184
		{ QStringLiteral("SL"), { "#AB84CD", "#6F389F" } },   // purple 272
		{ QStringLiteral("SR"), { "#CA7DA1", "#91305E" } },   // magenta 332
		{ QStringLiteral("SBL"), { "#B86FB8", "#9F419F" } },  // violet 300
		{ QStringLiteral("SBR"), { "#93BC76", "#558532" } },  // olive 95
	};
	const auto it = inks.constFind(ChannelIdentity::key(channel));
	if (it != inks.constEnd())
		return QColor(QLatin1String(dark ? it->dark : it->light));
	// Unmapped identities (the slate fallback, future channels): clamp the
	// identity colour toward the medium so nothing ever paints as a web
	// primary. The hue guard keeps a true neutral (reported hue -1) from
	// turning red.
	const QColor base = ChannelIdentity::color(channel);
	float h, s, l;
	base.getHslF(&h, &s, &l);
	if (h < 0.0f)
		h = 0.0f;
	return dark
		? QColor::fromHslF(h, qMin(s, 0.42f), 0.64f)
		: QColor::fromHslF(h, qMin(s, 0.52f), 0.38f);
}
