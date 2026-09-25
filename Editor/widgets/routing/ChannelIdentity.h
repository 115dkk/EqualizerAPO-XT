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
	Which channels are virtual (dashed) is decided here too, by isVirtual.
*/

#pragma once

#include <string>
#include <vector>

#include <QColor>
#include <QHash>
#include <QString>

#include "audio/ChannelLayout.h"

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

// The channel list the virtual rule judges against when the device is not
// known (no device selected, the gallery's deviceless card path): the 7.1
// layout the Editor's analysis runs with in the same situation
// (ChannelLayout::analysisLayout).
inline const std::vector<std::wstring>& unknownDeviceChannels()
{
	static const std::vector<std::wstring> names = [] {
		const ChannelLayout::AnalysisLayout layout = ChannelLayout::analysisLayout(0, 0, 0);
		return ChannelLayout::getChannelNames(static_cast<int>(layout.channelCount), layout.channelMask);
	}();
	return names;
}

// The one rule for which channels are virtual (audit #348 TD-44): a channel
// is virtual when it names none of the device's channels. It names one the
// way the engine resolves a Copy or MultiConvolution target
// (ChannelLayout::resolveTarget): by name, by alias (SL/RL, SR/RR, SUB for
// LFE) or by 1-based number, case-insensitive like the Editor's parsers. A
// target the engine cannot resolve becomes a new channel of that name, and
// that is what the views draw dashed. ALL selects every channel and is never
// virtual. An empty deviceChannels means the device is not known; the rule
// then judges against unknownDeviceChannels().
inline bool isVirtual(const QString& channel, const std::vector<std::wstring>& deviceChannels)
{
	const QString upper = channel.trimmed().toUpper();
	if (upper == QLatin1String("ALL"))
		return false;
	const std::vector<std::wstring>& names = deviceChannels.empty() ? unknownDeviceChannels() : deviceChannels;
	return ChannelLayout::getChannelIndex(upper.toStdWString(), names, true) < 0;
}
}
