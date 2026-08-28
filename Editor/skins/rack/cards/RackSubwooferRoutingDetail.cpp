/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSubwooferRoutingDetail.h"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QWidget>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/skins/shared/SkinPaint.h"

namespace RackSubwooferRoutingDetail
{

qreal physicalPixel(const QWidget* widget)
{
	return 1.0 / qMax<qreal>(1.0, widget->devicePixelRatioF());
}

qreal crispCoordinate(const QWidget* widget, qreal value)
{
	const qreal ratio = qMax<qreal>(1.0, widget->devicePixelRatioF());
	return (qFloor(value * ratio) + 0.5) / ratio;
}

QColor enabledInk(const QWidget* widget, const QColor& color, int enabledAlpha)
{
	return withAlpha(
		color,
		widget->isEnabled() ? enabledAlpha : qMin(enabledAlpha, 90));
}

QFont rackFont(const SkinTokens& tokens, int pixelSize, bool bold, qreal letterSpacing)
{
	QFont font(tokens.fontFamily);
	font.setPixelSize(pixelSize);
	font.setBold(bold);

	if (letterSpacing > 0.0)
		font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);

	return font;
}

QFont rackMonoFont(const SkinTokens& tokens, int pixelSize, bool bold, qreal letterSpacing)
{
	QFont font(tokens.monoFontFamily);

	if (tokens.monoFontFamily.isEmpty())
		font.setStyleHint(QFont::Monospace);

	font.setPixelSize(pixelSize);
	font.setBold(bold);

	if (letterSpacing > 0.0)
		font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);

	return font;
}

QString fittedText(
	const QString& text,
	const QFont& font,
	qreal availableWidth)
{
	if (availableWidth <= 0.0)
		return QString();

	return QFontMetrics(font).elidedText(
		text,
		Qt::ElideRight,
		qMax(0, qFloor(availableWidth)));
}

QString formattedDb(double value)
{
	if (!std::isfinite(value))
		return QStringLiteral("--");

	QString text = QString::number(value, 'f', 1);

	if (value > 0.0)
		text.prepend(QLatin1Char('+'));

	return text;
}

void drawEngravedText(
	QPainter& painter,
	const QWidget* widget,
	const QRectF& rect,
	int flags,
	const QString& text,
	const QFont& font,
	const QColor& ink)
{
	if (text.isEmpty() || rect.width() <= 0.0 || rect.height() <= 0.0)
		return;

	const QPalette::ColorGroup group = widget->isEnabled()
		? QPalette::Active
		: QPalette::Disabled;
	const QColor recess = widget->palette().color(group, QPalette::Shadow);

	painter.setFont(font);
	painter.setPen(enabledInk(widget, recess, 150));
	painter.drawText(
		rect.translated(0.0, physicalPixel(widget)),
		flags,
		text);
	painter.setPen(enabledInk(widget, ink));
	painter.drawText(rect, flags, text);
}

void drawCrispHorizontalLine(
	QPainter& painter,
	const QWidget* widget,
	qreal left,
	qreal right,
	qreal y,
	const QColor& color)
{
	if (right <= left)
		return;

	painter.setPen(QPen(color, physicalPixel(widget)));
	painter.drawLine(
		QPointF(
			crispCoordinate(widget, left),
			crispCoordinate(widget, y)),
		QPointF(
			crispCoordinate(widget, right),
			crispCoordinate(widget, y)));
}

void drawCrispVerticalLine(
	QPainter& painter,
	const QWidget* widget,
	qreal x,
	qreal top,
	qreal bottom,
	const QColor& color,
	qreal width)
{
	if (bottom <= top)
		return;

	painter.setPen(QPen(color, width * physicalPixel(widget)));
	painter.drawLine(
		QPointF(
			crispCoordinate(widget, x),
			crispCoordinate(widget, top)),
		QPointF(
			crispCoordinate(widget, x),
			crispCoordinate(widget, bottom)));
}

void setSeverityProperty(QWidget* widget, const QString& severity)
{
	if (widget == nullptr || widget->property("severity").toString() == severity)
		return;

	widget->setProperty("severity", severity);
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}

bool containsLabelCharacters(const QString& text)
{
	for (const QChar character : text)
	{
		if (character.isLetterOrNumber())
			return true;
	}

	return false;
}

}
