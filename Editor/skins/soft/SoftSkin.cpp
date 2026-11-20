/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// Soft skin. Constitution: docs/skins/soft.md. The file-scope instance is
// exposed through softSkin() so Skins::all() can assemble the roster
// without a central definition list.

#include "SoftSkin.h"

#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include "Editor/skins/shared/SkinSupport.h"
#include "cards/SoftReferenceCardView.h"
#include "cards/SoftSubwooferRoutingCardView.h"
#include "picker/SoftFilterPicker.h"
#include "routing/BlockChipRoutingRenderer.h"
#include "SoftPaint.h"

QString SoftSkin::id() const { return QStringLiteral("soft"); }
IRoutingRenderer* SoftSkin::routingRenderer() const
{
	static BlockChipRoutingRenderer renderer;
	return &renderer;
}

// The command picker (soft/picker/SoftFilterPicker.cpp).
FilterPickerView* SoftSkin::createFilterPicker(QWidget* parent, const SkinTokens& tokens) const
{
	return new SoftFilterPickerView(tokens, parent);
}

// The file-reference rows (soft/cards/SoftReferenceCardView.cpp).
ReferenceCardView* SoftSkin::createReferenceCardView(const QString& kind, QWidget* parent,
	const SkinTokens& tokens) const
{
	return new SoftReferenceCardView(kind, tokens, parent);
}

SubwooferRoutingCardView* SoftSkin::createSubwooferRoutingCardView(QWidget* parent,
	const SkinTokens& tokens) const
{
	return new SoftSubwooferRoutingCardView(tokens, parent);
}

// The card head's power control is a switch. The sheets draw its track
// (QToolButton#FilterCardIconButton[powerToggle="true"]); the thumb is this
// icon, a 34x18 picture with one disc at the left (off) or the right (on), so
// the two states differ by position as well as by the track's fill.
QIcon SoftSkin::powerToggleIcon(bool enabled, const SkinTokens& tokens) const
{
	const QColor thumb(enabled ? tokens.onAccent : tokens.mutedText);
	QIcon icon;
	for (const qreal ratio : { 1.0, 1.5, 2.0, 3.0 })
	{
		QPixmap pixmap(qRound(34 * ratio), qRound(18 * ratio));
		pixmap.setDevicePixelRatio(ratio);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(Qt::NoPen);
		painter.setBrush(thumb);
		painter.drawEllipse(QPointF(enabled ? 25.0 : 9.0, 9.0), 6.5, 6.5);
		painter.end();
		icon.addPixmap(pixmap);
		// A switched-off row disables nothing here, but a row that cannot be
		// toggled at all does: its thumb keeps the same picture, and the
		// sheet's :disabled rule fades the track.
		icon.addPixmap(pixmap, QIcon::Disabled);
	}
	return icon;
}

// A channel is a tag: the name in the body ink inside an outline. No colour
// per channel - the name is what tells L from R. A channel no device has
// (a virtual one) takes the dashed line.
bool SoftSkin::paintChannelBadge(QPainter& painter, const QRect& rect, const QString& channel,
	bool virtualChannel, const SkinTokens& tokens) const
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	const QRectF box = QRectF(rect).adjusted(1.0, 1.0, -1.0, -1.0);
	SoftPaint::box(painter, box, QColor(), QColor(virtualChannel ? tokens.controlEdge : tokens.borderStrong),
		box.height() / 2.0, 1.0, virtualChannel ? Qt::DashLine : Qt::SolidLine);
	painter.setFont(SoftPaint::bodyFont(tokens, 12));
	painter.setPen(QColor(tokens.text));
	painter.drawText(box, Qt::AlignCenter, channel);
	painter.restore();
	return true;
}

// A control that cannot be used at all is not shown (docs/skins/soft.md,
// rule 6): a VST2 plugin has no layouts to choose, so its row has no bus
// strip. The row's format tag already says VST2.
bool SoftSkin::showsUnusableVstBusStrip() const
{
	return false;
}

// Window chrome: no paintTitleBarChrome override. The title bar is the
// window ground, a title and three bare glyphs, all in the sheets.

// tokens()/qssResource() ride the ISkin defaults (SkinThemeData tables).

ISkin* softSkin()
{
	static SoftSkin instance;
	return &instance;
}
