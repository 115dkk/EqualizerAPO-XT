/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

// The trailing add row (shared insertion contract,
// docs/skins/README.md): a full-height dashed stadium slot - the
// "nothing vouches for this yet" edge, not a sleeping slot - with a
// quiet sunken "+" disc waiting at the centre.
void SoftSkin::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	painter.setRenderHint(QPainter::Antialiasing);

	const QColor accent(tokens.accent);
	const QColor warmInk(QStringLiteral("#2B251D"));
	const bool lifted = state.hovered || state.pressed;

	QRectF frame = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = frame.height() / 2.0;

	// Hover: the slot rises one value step above the window (no shadow -
	// the two-step elevation rule fakes it with the fill + light border).
	if (lifted)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(tokens.surface));
		painter.drawRoundedRect(frame, radius, radius);
	}

	// Keyboard focus: the quiet halo (alpha 90, 3px), not a hard ring.
	if (state.focused)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(frame, radius, radius);
	}

	QPen outline(lifted ? withAlpha(accent, state.pressed ? 210 : 150) : QColor(tokens.border), 1, Qt::DashLine);
	outline.setCapStyle(Qt::RoundCap);
	painter.setPen(outline);
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(frame, radius, radius);

	// Centred friendly composition: the "+" disc and the caption.
	QFont font(tokens.fontFamily);
	font.setPointSizeF(11.0);
	font.setWeight(QFont::DemiBold);
	const QFontMetricsF metrics(font);
	const qreal discD = 24.0;
	const qreal gap = 10.0;
	const QString caption = metrics.elidedText(state.label, Qt::ElideRight,
		int(qMax<qreal>(40.0, frame.width() - discD - gap - 48.0)));
	const qreal textW = metrics.horizontalAdvance(caption);
	const qreal left = frame.center().x() - (discD + gap + textW) / 2.0;
	QRectF discRect(left, frame.center().y() - discD / 2.0, discD, discD);

	if (state.pressed)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(mixColor(accent, warmInk, 0.18));
	}
	else if (state.hovered)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(accent);
	}
	else
	{
		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.setBrush(QColor(tokens.surfaceSunken));
	}
	painter.drawEllipse(discRect);

	QPen plusPen(lifted ? warmInk : QColor(tokens.mutedText), 2.4, Qt::SolidLine, Qt::RoundCap);
	painter.setPen(plusPen);
	const QPointF discCenter = discRect.center();
	const qreal arm = discD * 0.21;
	painter.drawLine(QPointF(discCenter.x() - arm, discCenter.y()), QPointF(discCenter.x() + arm, discCenter.y()));
	painter.drawLine(QPointF(discCenter.x(), discCenter.y() - arm), QPointF(discCenter.x(), discCenter.y() + arm));

	painter.setFont(font);
	painter.setPen(lifted ? QColor(tokens.text) : QColor(tokens.mutedText));
	painter.drawText(QRectF(left + discD + gap, frame.top(), textW + 4.0, frame.height()),
		Qt::AlignVCenter | Qt::AlignLeft, caption);
}

// The first-boundary insertion seam: a pastel pill line led by a round
// "+" disc. At rest the widget paints nothing (shared contract).
void SoftSkin::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	if (!state.hovered && !state.pressed)
		return;

	painter.setRenderHint(QPainter::Antialiasing);
	const QColor accent(tokens.accent);
	const QColor warmInk(QStringLiteral("#2B251D"));
	QRectF r(rect);
	const qreal cy = r.center().y();
	const qreal discR = qMin<qreal>(9.0, r.height() / 2.0);
	const qreal discCx = r.left() + discR + 4.0;

	const qreal lineH = qBound<qreal>(3.0, r.height() * 0.5, 5.0);
	QRectF bar(discCx + discR + 6.0, cy - lineH / 2.0,
		r.right() - 4.0 - (discCx + discR + 6.0), lineH);
	painter.setPen(Qt::NoPen);
	painter.setBrush(mixColor(accent, QColor(tokens.card), 0.25));
	painter.drawRoundedRect(bar, lineH / 2.0, lineH / 2.0);

	painter.setBrush(state.pressed ? mixColor(accent, warmInk, 0.18) : accent);
	painter.drawEllipse(QPointF(discCx, cy), discR, discR);

	QPen plusPen(warmInk, qMax<qreal>(1.6, discR * 0.36), Qt::SolidLine, Qt::RoundCap);
	painter.setPen(plusPen);
	const qreal arm = discR * 0.45;
	painter.drawLine(QPointF(discCx - arm, cy), QPointF(discCx + arm, cy));
	painter.drawLine(QPointF(discCx, cy - arm), QPointF(discCx, cy + arm));
}
