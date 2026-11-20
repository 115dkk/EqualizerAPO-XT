/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FlatSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "FlatPaint.h"

namespace
{
void paintPlus(QPainter& painter, const QPointF& center, qreal arm, const QColor& ink, qreal width)
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(ink, width, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(QPointF(center.x() - arm, center.y()), QPointF(center.x() + arm, center.y()));
	painter.drawLine(QPointF(center.x(), center.y() - arm), QPointF(center.x(), center.y() + arm));
	painter.restore();
}
}

// The trailing add row (shared insertion contract, docs/skins/README.md) is
// an ordinary button at the start of the row: a plus and the words. The rest
// of the row is the window; the whole row still takes the click.
void FlatSkin::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QFont font = FlatPaint::bodyFont(tokens, 14);
	const QFontMetricsF metrics(font);
	const qreal padding = 14.0;
	const qreal glyph = 22.0;
	const qreal height = qMin<qreal>(36.0, rect.height());
	const QString caption = metrics.elidedText(state.label, Qt::ElideRight,
		int(qMax<qreal>(40.0, rect.width() - padding * 2.0 - glyph)));
	const qreal width = qMin<qreal>(rect.width(), padding * 2.0 + glyph + metrics.horizontalAdvance(caption) + 2.0);
	const QRectF button(rect.left(), rect.center().y() + 0.5 - height / 2.0, width, height);

	QColor fill(tokens.card);
	QColor edge(tokens.borderStrong);
	qreal edgeWidth = 1.0;
	if (state.pressed)
	{
		fill = QColor(tokens.cardSelected);
		edge = QColor(tokens.controlEdge);
	}
	else if (state.hovered)
	{
		fill = QColor(tokens.cardHover);
		edge = QColor(tokens.controlEdge);
	}
	if (state.focused)
	{
		edge = QColor(tokens.focusRing);
		edgeWidth = 2.0;
	}
	FlatPaint::box(painter, button, fill, edge, FlatPaint::controlRadius, edgeWidth);

	paintPlus(painter, QPointF(button.left() + padding + 6.0, button.center().y()), 4.5, QColor(tokens.text), 1.5);
	painter.setFont(font);
	painter.setPen(QColor(tokens.text));
	painter.drawText(QRectF(button.left() + padding + glyph, button.top(), button.width() - padding * 2.0 - glyph + 4.0,
		button.height()), Qt::AlignVCenter | Qt::AlignLeft, caption);
}

// The first-boundary insertion seam: a line in the emphasis colour where the
// new card will go, led by a small round plus. At rest the widget paints
// nothing (shared contract).
void FlatSkin::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	if (!state.hovered && !state.pressed)
		return;
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QColor accent(tokens.accent);
	const QRectF r(rect);
	const qreal cy = qFloor(r.center().y()) + 0.5;
	const qreal discR = qMin<qreal>(8.0, r.height() / 2.0);
	const qreal discCx = r.left() + discR + 2.0;
	painter.fillRect(QRectF(discCx + discR + 4.0, cy - 1.0, r.right() - (discCx + discR + 4.0), 2.0), accent);
	painter.setPen(Qt::NoPen);
	painter.setBrush(state.pressed ? mixColor(accent, QColor(tokens.onAccent), 0.25) : accent);
	painter.drawEllipse(QPointF(discCx, cy), discR, discR);
	paintPlus(painter, QPointF(discCx, cy), discR * 0.45, QColor(tokens.onAccent), 1.5);
}
