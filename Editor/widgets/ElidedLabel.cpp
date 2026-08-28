/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ElidedLabel.h"

#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

ElidedLabel::ElidedLabel(QWidget* parent)
	: QLabel(parent)
{
	// The label must be allowed to shrink below its text's natural width,
	// otherwise the layout never gives it a width that needs eliding.
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ElidedLabel::setFullText(const QString& text)
{
	storedText = text;
	// QLabel keeps the full text so sizeHint stays honest for height purposes;
	// painting below substitutes the elided form.
	QLabel::setText(text);
	setToolTip(text);
}

const QString& ElidedLabel::fullText() const
{
	return storedText;
}

void ElidedLabel::setElideMode(Qt::TextElideMode mode)
{
	elideMode = mode;
	update();
}

QSize ElidedLabel::minimumSizeHint() const
{
	// Do not let the full text dictate the minimum width; the whole point of
	// the label is to survive narrow layouts.
	QSize hint = QLabel::minimumSizeHint();
	hint.setWidth(fontMetrics().horizontalAdvance(QStringLiteral("...")) + 2);
	return hint;
}

void ElidedLabel::paintEvent(QPaintEvent* event)
{
	if (storedText.isEmpty())
	{
		QLabel::paintEvent(event);
		return;
	}

	const QString elided = fontMetrics().elidedText(storedText, elideMode, contentsRect().width());
	if (elided == storedText)
	{
		QLabel::paintEvent(event);
		return;
	}

	// Same palette/state resolution QLabel uses, so QSS colours apply.
	QPainter painter(this);
	QStyleOption option;
	option.initFrom(this);
	style()->drawItemText(&painter, contentsRect(), alignment(), option.palette,
		isEnabled(), elided, foregroundRole());
}
