/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QtGlobal>

class QPainter;
class QRectF;
class QWidget;
struct SkinTokens;

namespace RackSubwooferRoutingDetail
{
qreal physicalPixel(const QWidget* widget);
qreal crispCoordinate(const QWidget* widget, qreal value);
QColor enabledInk(const QWidget* widget, const QColor& color, int enabledAlpha = 255);
QFont rackFont(const SkinTokens& tokens, int pixelSize, bool bold, qreal letterSpacing = 0.0);
QFont rackMonoFont(const SkinTokens& tokens, int pixelSize, bool bold, qreal letterSpacing = 0.0);
QString fittedText(const QString& text, const QFont& font, qreal availableWidth);
QString formattedDb(double value);
void drawEngravedText(QPainter& painter, const QWidget* widget, const QRectF& rect,
	int flags, const QString& text, const QFont& font, const QColor& ink);
void drawCrispHorizontalLine(QPainter& painter, const QWidget* widget, qreal left,
	qreal right, qreal y, const QColor& color);
void drawCrispVerticalLine(QPainter& painter, const QWidget* widget, qreal x,
	qreal top, qreal bottom, const QColor& color, qreal width = 1.0);
void setSeverityProperty(QWidget* widget, const QString& severity);
bool containsLabelCharacters(const QString& text);
}
