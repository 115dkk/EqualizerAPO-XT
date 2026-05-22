#pragma once

#include <QString>

struct SkinTokens
{
	enum GroupStyle
	{
		TreeLines,
		GradientBar,
		DottedLine,
		SoftShadow
	};

	enum BadgeStyle
	{
		OutlineOnly,
		ColorPill,
		WireframeBorder,
		SoftPill
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
	QString text = QStringLiteral("#e8e8f4");
	QString mutedText = QStringLiteral("#8888a8");
	QString border = QStringLiteral("#2a2a3c");
	QString graph = QStringLiteral("#08080f");
	QString accent = QStringLiteral("#3B82F6");
	bool zebraStripe = false;
};
