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
*/

#pragma once

#include <QColor>
#include <QHash>
#include <QString>

inline QColor minimalChannelInk(const QColor& base, bool dark)
{
	struct Ink { const char* dark; const char* light; };
	static const QHash<QString, Ink> inks = {
		{ QStringLiteral("#ef4444"), { "#CC7578", "#972B2E" } },  // L    red 358
		{ QStringLiteral("#3b82f6"), { "#809BD0", "#2E539E" } },  // R    blue 220
		{ QStringLiteral("#22c55e"), { "#79C38C", "#2D8042" } },  // C    green 135
		{ QStringLiteral("#f59e0b"), { "#C8B879", "#8A7728" } },  // LFE  yellow 48
		{ QStringLiteral("#f97316"), { "#C39779", "#8C5531" } },  // RL   orange 24
		{ QStringLiteral("#06b6d4"), { "#7DC5CA", "#2E848A" } },  // RR   cyan 184
		{ QStringLiteral("#a855f7"), { "#AB84CD", "#6F389F" } },  // SL   purple 272
		{ QStringLiteral("#ec4899"), { "#CA7DA1", "#91305E" } },  // SR   magenta 332
		{ QStringLiteral("#8b5cf6"), { "#B86FB8", "#9F419F" } },  // SBL  violet 300
		{ QStringLiteral("#14b8a6"), { "#93BC76", "#558532" } },  // SBR  olive 95
	};
	const auto it = inks.constFind(base.name());
	if (it != inks.constEnd())
		return QColor(QLatin1String(dark ? it->dark : it->light));
	// Unmapped identities (the slate fallback, future channels): clamp the
	// base toward the medium so nothing ever paints as a web primary. The
	// hue guard keeps a true neutral (reported hue -1) from turning red.
	float h, s, l;
	base.getHslF(&h, &s, &l);
	if (h < 0.0f)
		h = 0.0f;
	return dark
		? QColor::fromHslF(h, qMin(s, 0.42f), 0.64f)
		: QColor::fromHslF(h, qMin(s, 0.52f), 0.38f);
}
