/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SkinComboBox.h"

#include <QAbstractItemView>
#include <QFontMetrics>

namespace
{
// Logical px scale with Qt's device pixel ratio, so the constant part is
// DPI-aware by itself; the font term keeps the floor honest when the user
// runs a larger system font.
int heightFloor(const QFontMetrics& metrics)
{
	return qMax(28, metrics.height() + 12);
}
}

SkinComboBox::SkinComboBox(QWidget* parent)
	: QComboBox(parent)
{
}

QSize SkinComboBox::sizeHint() const
{
	QSize size = QComboBox::sizeHint();
	size.setHeight(qMax(size.height(), heightFloor(fontMetrics())));
	return size;
}

QSize SkinComboBox::minimumSizeHint() const
{
	QSize size = QComboBox::minimumSizeHint();
	size.setHeight(qMax(size.height(), heightFloor(fontMetrics())));
	return size;
}

void SkinComboBox::showPopup()
{
	// Make the popup at least as wide as its longest item so entries are not
	// elided to the closed control's width. The extra padding covers the QSS
	// item padding plus a vertical scrollbar.
	const QFontMetrics metrics(font());
	int widest = 0;
	for (int i = 0; i < count(); i++)
		widest = qMax(widest, metrics.horizontalAdvance(itemText(i)));
	view()->setMinimumWidth(qMax(width(), widest + 40));

	QComboBox::showPopup();
}
