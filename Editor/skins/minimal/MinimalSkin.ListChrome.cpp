/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MinimalSkin.h"

#include <QPainter>
#include <QtMath>

// The trailing add row is the terminal's input prompt line: "+ ADD
// FILTER" as an uppercase tracked mono caption inside a 1px hairline
// slot. No dashes: a dashed hairline means "no verified substance" in
// this skin's chip grammar, and this slot is a real command.
void MinimalSkin::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	QColor ink(tokens.mutedText);
	QColor edge(tokens.border);
	if (state.pressed)
	{
		painter.fillRect(rect, QColor(tokens.text));
		ink = QColor(tokens.surface);
		edge = QColor(tokens.text);
	}
	else if (state.hovered)
	{
		// One ground step plus the comment card's hover law: the caption
		// ink lifts to body brightness because the line acts on click.
		painter.fillRect(rect, QColor(tokens.surface));
		ink = QColor(tokens.text);
	}
	if (state.focused && !state.pressed)
		edge = QColor(tokens.focusRing);
	painter.setPen(QPen(edge, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(rect.adjusted(0, 0, -1, -1));

	QFont font(tokens.monoFontFamily);
	font.setPointSizeF(10.0);
	font.setWeight(QFont::Bold);
	font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	painter.setFont(font);
	painter.setPen(ink);
	painter.drawText(rect.adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft,
		QStringLiteral("+ ") + state.label.toUpper());
}
// The first-boundary seam: a text editor's insert line. The widget only
// shows itself while hovered, so at rest nothing is painted anywhere.
void MinimalSkin::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	if (!state.hovered && !state.pressed)
		return;
	const QColor accent(tokens.accent);
	const int centerY = rect.center().y();
	const int side = qMin(rect.height(), 12);
	const QRect cell(rect.left(), centerY - side / 2, side, side);

	painter.setPen(QPen(accent, 1));
	painter.setBrush(state.pressed ? QBrush(accent) : Qt::NoBrush);
	painter.drawRect(cell.adjusted(0, 0, -1, -1));
	painter.drawLine(cell.right() + 5, centerY, rect.right(), centerY);

	QFont font(tokens.monoFontFamily);
	font.setPixelSize(qMax(7, side - 3));
	font.setWeight(QFont::Bold);
	painter.setFont(font);
	painter.setPen(state.pressed ? QColor(tokens.background) : accent);
	painter.drawText(cell, Qt::AlignCenter, QStringLiteral("+"));
}
