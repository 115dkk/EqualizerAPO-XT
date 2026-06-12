/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2016  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "GUIHelper.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QScreen>
#include <QStyleHints>

#include "Editor/SkinManager.h"

QSize GUIHelper::scale(QSize size)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return size;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return QSize(qRound(size.width() * dpi / 96), qRound(size.height() * dpi / 96));
}

int GUIHelper::scale(double pixel)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return qRound(pixel);

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return qRound(pixel * dpi / 96);
}

double GUIHelper::scaleZoom(double zoom)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return zoom;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return zoom * dpi / 96;
}

double GUIHelper::invScale(int pixel)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return pixel;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return pixel * 96 / dpi;
}

double GUIHelper::invScaleZoom(double zoom)
{
	if (qApp->testAttribute(Qt::AA_Use96Dpi))
		return zoom;

	qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInchX();
	return zoom * 96 / dpi;
}

bool GUIHelper::isDarkMode()
{
	return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QIcon GUIHelper::tintedIcon(const QString& resource, const QColor& color, int size)
{
	QIcon base(resource);
	QPixmap pixmap = base.pixmap(scale(QSize(size, size)));
	if (pixmap.isNull())
		return base;

	// Keep the rendered glyph's alpha mask, replace its colour. CompositionMode_SourceIn
	// paints the fill only where the source already has coverage, so the result is the
	// same shape in the requested colour. The fill rect is in device-independent units;
	// any overshoot on a high-DPI pixmap is harmless because untouched pixels stay clear.
	QPainter painter(&pixmap);
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.fillRect(QRect(QPoint(0, 0), pixmap.deviceIndependentSize().toSize()), color);
	painter.end();

	return QIcon(pixmap);
}

void GUIHelper::applySkinPalette()
{
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const bool dark = SkinManager::instance()->isDark();
	QPalette palette = qApp->palette();
	QColor background(tokens.background);
	QColor surface(tokens.surface);
	QColor card(tokens.card);
	QColor text(tokens.text);
	QColor accent(tokens.accent);
	palette.setColor(QPalette::Window, background);
	palette.setColor(QPalette::WindowText, text);
	palette.setColor(QPalette::Base, surface);
	palette.setColor(QPalette::AlternateBase, card);
	palette.setColor(QPalette::Text, text);
	palette.setColor(QPalette::Button, card);
	palette.setColor(QPalette::ButtonText, text);
	palette.setColor(QPalette::ToolTipBase, card);
	palette.setColor(QPalette::ToolTipText, text);
	palette.setColor(QPalette::Highlight, accent);
	palette.setColor(QPalette::HighlightedText, dark ? QColor(QStringLiteral("#0c0c16")) : QColor(QStringLiteral("#ffffff")));
	palette.setColor(QPalette::PlaceholderText, QColor(tokens.mutedText));
	palette.setColor(QPalette::Light, card.lighter(120));
	palette.setColor(QPalette::Midlight, card.lighter(105));
	palette.setColor(QPalette::Mid, surface);
	palette.setColor(QPalette::Dark, background.darker(120));
	palette.setColor(QPalette::Shadow, background.darker(160));
	palette.setColor(QPalette::Link, accent);
	palette.setColor(QPalette::LinkVisited, accent.darker(110));
	qApp->setPalette(palette);
}
