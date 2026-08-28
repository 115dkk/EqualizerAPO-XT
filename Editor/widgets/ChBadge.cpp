/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ChBadge.h"

#include <QHash>
#include <QPainter>

#include "Editor/SkinManager.h"

ChBadge::ChBadge(QWidget* parent)
	: QWidget(parent)
{
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		update();
	});
}

ChBadge::ChBadge(const QString& channel, QWidget* parent)
	: ChBadge(parent)
{
	setChannel(channel);
}

const QString& ChBadge::channel() const
{
	return currentChannel;
}

void ChBadge::setChannel(const QString& channel)
{
	currentChannel = channel.trimmed().toUpper();
	updateGeometry();
	update();
}

QSize ChBadge::sizeHint() const
{
	int width = qMax(26, fontMetrics().horizontalAdvance(currentChannel) + 14);
	return QSize(width, 20);
}

bool ChBadge::isVirtualChannel() const
{
	return currentChannel.startsWith('V');
}

QColor ChBadge::channelColor() const
{
	static const QHash<QString, QColor> colors = {
		{ QStringLiteral("L"), QColor("#ef4444") },
		{ QStringLiteral("R"), QColor("#f97316") },
		{ QStringLiteral("C"), QColor("#eab308") },
		{ QStringLiteral("SUB"), QColor("#22c55e") },
		{ QStringLiteral("SL"), QColor("#06b6d4") },
		{ QStringLiteral("SR"), QColor("#6366f1") },
		{ QStringLiteral("RL"), QColor("#a855f7") },
		{ QStringLiteral("RR"), QColor("#ec4899") },
		{ QStringLiteral("ALL"), QColor("#94a3b8") }
	};

	QString baseChannel = currentChannel;
	if (baseChannel.startsWith('V'))
		baseChannel = baseChannel.mid(1);
	return colors.value(baseChannel, QColor("#64748b"));
}

void ChBadge::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	if (SkinManager::instance()->paintChannelBadge(painter, rect(), currentChannel, isVirtualChannel()))
		return;

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	QColor color = channelColor();
	QRectF badgeRect = rect().adjusted(1, 2, -1, -2);
	int radius = tokens.badgeStyle == SkinTokens::OutlineOnly || tokens.badgeStyle == SkinTokens::WireframeBorder ? tokens.borderRadius : badgeRect.height() / 2;

	if (tokens.badgeStyle == SkinTokens::OutlineOnly || tokens.badgeStyle == SkinTokens::WireframeBorder || isVirtualChannel())
	{
		QPen pen(color, 1.2);
		if (isVirtualChannel())
			pen.setStyle(Qt::DashLine);
		painter.setPen(pen);
		QColor fill = color;
		fill.setAlpha(SkinManager::instance()->isDark() ? 35 : 22);
		painter.setBrush(fill);
	}
	else
	{
		// Filled style: tone down the saturated channel colour to a soft chip
		// (low-alpha fill + matching outline). A fully-saturated brush reads
		// like a warning indicator next to neutral header controls, even
		// though the row is in a perfectly normal selection state.
		QColor fill = color;
		fill.setAlpha(SkinManager::instance()->isDark() ? 70 : 48);
		QPen pen(color, 1.0);
		painter.setPen(pen);
		painter.setBrush(fill);
	}

	painter.drawRoundedRect(badgeRect, radius, radius);
	// Text always uses the channel colour; a pure-white glyph on the muted
	// chip's soft pastel background is hard to read.
	painter.setPen(color);
	QFont badgeFont = font();
	badgeFont.setBold(true);
	badgeFont.setPointSizeF(qMax(7.5, badgeFont.pointSizeF() - 1.0));
	painter.setFont(badgeFont);
	painter.drawText(rect(), Qt::AlignCenter, currentChannel);
}
