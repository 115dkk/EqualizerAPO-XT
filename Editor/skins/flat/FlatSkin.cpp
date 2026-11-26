/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// Flat skin. Constitution: docs/skins/flat.md. The file-scope instance is
// exposed through flatSkin() so Skins::all() can assemble the roster
// without a central definition list.

#include "FlatSkin.h"

#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include "Editor/skins/shared/SkinSupport.h"
#include "cards/FlatReferenceCardView.h"
#include "picker/FlatFilterPicker.h"
#include "routing/BlockChipRoutingRenderer.h"
#include "FlatPaint.h"

QString FlatSkin::id() const { return QStringLiteral("flat"); }
IRoutingRenderer* FlatSkin::routingRenderer() const
{
	static BlockChipRoutingRenderer renderer;
	return &renderer;
}

// The command picker (flat/picker/FlatFilterPicker.cpp).
FilterPickerView* FlatSkin::createFilterPicker(QWidget* parent, const SkinTokens& tokens) const
{
	return new FlatFilterPickerView(tokens, parent);
}

// The file-reference rows (flat/cards/FlatReferenceCardView.cpp).
ReferenceCardView* FlatSkin::createReferenceCardView(const QString& kind, QWidget* parent,
	const SkinTokens& tokens) const
{
	return new FlatReferenceCardView(kind, tokens, parent);
}

// The card head's power control is a switch. The sheets draw its track
// (QToolButton#FilterCardIconButton[powerToggle="true"]); the thumb is this
// icon, a 34x18 picture with one disc at the left (off) or the right (on), so
// the two states differ by position as well as by the track's fill.
QIcon FlatSkin::powerToggleIcon(bool enabled, const SkinTokens& tokens) const
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
bool FlatSkin::paintChannelBadge(QPainter& painter, const QRect& rect, const QString& channel,
	bool virtualChannel, const SkinTokens& tokens) const
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	const QRectF box = QRectF(rect).adjusted(1.0, 1.0, -1.0, -1.0);
	FlatPaint::box(painter, box, QColor(), QColor(virtualChannel ? tokens.controlEdge : tokens.borderStrong),
		box.height() / 2.0, 1.0, virtualChannel ? Qt::DashLine : Qt::SolidLine);
	painter.setFont(FlatPaint::bodyFont(tokens, 12));
	painter.setPen(QColor(tokens.text));
	painter.drawText(box, Qt::AlignCenter, channel);
	painter.restore();
	return true;
}

// A control that cannot be used at all is not shown (docs/skins/flat.md,
// rule 6): a VST2 plugin has no layouts to choose, so its row has no bus
// strip. The row's format tag already says VST2.
bool FlatSkin::showsUnusableVstBusStrip() const
{
	return false;
}

// Window chrome: no paintTitleBarChrome override. The title bar is the
// window ground, a title and three bare glyphs, all in the sheets.

// tokens()/qssResource() ride the ISkin defaults (SkinThemeData tables).

ISkin* flatSkin()
{
	static FlatSkin instance;
	return &instance;
}
