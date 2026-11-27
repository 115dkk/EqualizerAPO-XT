/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

#include "Editor/SkinTokens.h"

// The handful of shapes the flat skin is made of (docs/skins/flat.md): a box
// with a 1px line, a chevron, a state lamp. Every painter of the skin draws
// its parts through these, so a control looks the same wherever it appears.
namespace FlatPaint
{
// The two corner radii of the skin: surfaces (cards, popups, plot panels) and
// controls (buttons, fields, selects, chips).
constexpr qreal surfaceRadius = 10.0;
constexpr qreal controlRadius = 6.0;
// The one height, outline included, of every cap in the filter list:
// buttons, selects, chips, segmented controls and the fields on a cap line.
// The caps were 28 to 37 px with 7 to 11 px clear above and below a 14 px
// legend; half that air leaves 26 px.
constexpr int listCapHeight = 26;
// What a switched-off or unavailable thing is drawn at. It only fades; no
// dashes, no sinking.
constexpr qreal disabledOpacity = 0.45;

// A filled rounded box whose border line lies wholly inside rect, so boxes
// that touch share their edge pixel instead of doubling it.
inline void box(QPainter& painter, const QRectF& rect, const QColor& fill, const QColor& line,
	qreal radius, qreal lineWidth = 1.0, Qt::PenStyle style = Qt::SolidLine)
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	QRectF r = rect;
	if (line.isValid() && lineWidth > 0.0)
	{
		QPen pen(line, lineWidth, style);
		if (style == Qt::DashLine)
			pen.setDashPattern({ 3.0, 3.0 });
		painter.setPen(pen);
		const qreal half = lineWidth / 2.0;
		r.adjust(half, half, -half, -half);
		radius = qMax<qreal>(0.0, radius - half);
	}
	else
		painter.setPen(Qt::NoPen);
	painter.setBrush(fill.isValid() ? QBrush(fill) : QBrush(Qt::NoBrush));
	painter.drawRoundedRect(r, radius, radius);
	painter.restore();
}

// The down chevron of a select, the right chevron of a folded section.
inline void chevron(QPainter& painter, const QPointF& center, const QColor& ink, bool down = true,
	qreal half = 3.5, qreal width = 1.5)
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(ink, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	QPainterPath path;
	if (down)
	{
		path.moveTo(center + QPointF(-half, -half / 2.0));
		path.lineTo(center + QPointF(0.0, half / 2.0));
		path.lineTo(center + QPointF(half, -half / 2.0));
	}
	else
	{
		path.moveTo(center + QPointF(-half / 2.0, -half));
		path.lineTo(center + QPointF(half / 2.0, 0.0));
		path.lineTo(center + QPointF(-half / 2.0, half));
	}
	painter.drawPath(path);
	painter.restore();
}

// A state said once: one dot. Hollow means "off", not a state colour.
inline void lamp(QPainter& painter, const QPointF& center, const QColor& color, bool hollow = false)
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	if (hollow)
	{
		painter.setPen(QPen(color, 1.5));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(center, 3.25, 3.25);
	}
	else
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(color);
		painter.drawEllipse(center, 4.0, 4.0);
	}
	painter.restore();
}

inline QFont bodyFont(const SkinTokens& tokens, int pixelSize, QFont::Weight weight = QFont::Medium)
{
	QFont font(tokens.fontFamily);
	font.setPixelSize(pixelSize);
	font.setWeight(weight);
	return font;
}

// Numbers that change in place keep their columns.
inline QFont numberFont(const SkinTokens& tokens, int pixelSize, QFont::Weight weight = QFont::Medium)
{
	QFont font = bodyFont(tokens, pixelSize, weight);
	font.setFeature("tnum", 1);
	return font;
}

// Source text - a command line, a condition - is the one place the skin uses
// a monospace face. Coding ligatures are off: "==" must stay two characters.
inline QFont sourceFont(const SkinTokens& tokens, int pixelSize)
{
	QFont font(tokens.monoFontFamily);
	font.setPixelSize(pixelSize);
	font.setFeature("calt", 0);
	font.setFeature("liga", 0);
	return font;
}
}
