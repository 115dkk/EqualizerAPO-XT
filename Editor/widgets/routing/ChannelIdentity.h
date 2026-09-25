/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The one channel identity palette. A channel wears one colour everywhere
	it is named: the Copy routing views, the Channel row's items and the
	header channel badges (ChBadge) all ask here, and the minimal skin keys
	its designed console inks by the same canonical names (audit #348
	B5/TD-44, maintainer decision: the routing palette is the identity).
*/

#pragma once

#include <QColor>
#include <QHash>
#include <QString>

namespace ChannelIdentity
{
// The fixed per-channel hues, keyed by canonical channel name.
inline const QHash<QString, QString>& palette()
{
	static const QHash<QString, QString> colors = {
		{ QStringLiteral("L"), QStringLiteral("#ef4444") },
		{ QStringLiteral("R"), QStringLiteral("#3b82f6") },
		{ QStringLiteral("C"), QStringLiteral("#22c55e") },
		{ QStringLiteral("LFE"), QStringLiteral("#f59e0b") },
		{ QStringLiteral("SUB"), QStringLiteral("#f59e0b") },
		{ QStringLiteral("SL"), QStringLiteral("#a855f7") },
		{ QStringLiteral("SR"), QStringLiteral("#ec4899") },
		{ QStringLiteral("RL"), QStringLiteral("#f97316") },
		{ QStringLiteral("RR"), QStringLiteral("#06b6d4") },
		{ QStringLiteral("SBL"), QStringLiteral("#8b5cf6") },
		{ QStringLiteral("SBR"), QStringLiteral("#14b8a6") }
	};
	return colors;
}

// The neutral slate every channel outside the palette wears (virtual
// channels with no known base, numbered channels, ALL).
inline QString neutralColorName()
{
	return QStringLiteral("#94a3b8");
}

// The palette name a channel resolves to, or an empty string when it has
// none: case-insensitive, and a virtual channel (VSL, VRR) takes its base
// channel's identity.
inline QString key(const QString& channel)
{
	const QString upper = channel.toUpper();
	if (palette().contains(upper))
		return upper;
	if (upper.startsWith(QLatin1Char('V')) && upper.size() > 1)
	{
		const QString base = upper.mid(1);
		if (palette().contains(base))
			return base;
	}
	return QString();
}

// The identity colour of a channel as a "#rrggbb" string.
inline QString colorName(const QString& channel)
{
	const QString name = key(channel);
	return name.isEmpty() ? neutralColorName() : palette().value(name);
}

// The identity colour of a channel.
inline QColor color(const QString& channel)
{
	return QColor(colorName(channel));
}
}
