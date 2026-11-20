/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

struct SkinTokens
{
	bool dark = true;

	enum GroupStyle
	{
		TreeLines,
		GradientBar,
		DottedLine
	};

	enum BadgeStyle
	{
		OutlineOnly,
		ColorPill,
		WireframeBorder
	};

	int borderRadius = 10;
	int rowHeight = 40;
	int channelGroupIndent = 18;
	QString fontFamily = QStringLiteral("Segoe UI");
	QString monoFontFamily = QStringLiteral("Consolas");
	GroupStyle channelGroupStyle = GradientBar;
	BadgeStyle badgeStyle = ColorPill;
	QString background = QStringLiteral("#0c0c16");
	QString surface = QStringLiteral("#12121e");
	QString card = QStringLiteral("#1a1a28");
	QString cardHover = QStringLiteral("#202032");
	QString surfaceRaised = QStringLiteral("#202032");
	QString surfaceSunken = QStringLiteral("#08080f");
	QString cardSelected = QStringLiteral("#253b63");
	QString text = QStringLiteral("#e8e8f4");
	QString mutedText = QStringLiteral("#8888a8");
	QString border = QStringLiteral("#2a2a3c");
	QString graph = QStringLiteral("#08080f");
	QString graphGridMajor = QStringLiteral("#2a2a3c");
	QString graphGridMinor = QStringLiteral("#202032");
	QString accent = QStringLiteral("#3B82F6");
	QString accent2 = QStringLiteral("#8B5CF6");
	QString success = QStringLiteral("#22c55e");
	QString warning = QStringLiteral("#f59e0b");
	QString danger = QStringLiteral("#ef4444");
	QString focusRing = QStringLiteral("#3B82F6");
	// A line one step heavier than border, the edge of a control that has to
	// be found (3:1 on the card), and the ink on an accent fill. A skin table
	// may leave them empty; finishTokens then derives them from border,
	// mutedText and the theme. Only the flat sheets and painters read them.
	QString borderStrong;
	QString controlEdge;
	QString onAccent;
	// Width (in px) of the coloured rail drawn on the left edge of each filter
	// card. Used by Signal Matrix to make the routing structure visible. 0 means
	// no rail.
	int cardRailWidth = 0;
};
