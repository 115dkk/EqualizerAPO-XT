/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ActivatableListChrome.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>

ActivatableListChrome::ActivatableListChrome(QWidget* parent)
	: QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::PointingHandCursor);
}

bool ActivatableListChrome::isHovered() const
{
	return hovered;
}

bool ActivatableListChrome::isPressed() const
{
	return pressed;
}

void ActivatableListChrome::enterEvent(QEnterEvent*)
{
	hovered = true;
	update();
}

void ActivatableListChrome::leaveEvent(QEvent*)
{
	hovered = false;
	pressed = false;
	update();
}

void ActivatableListChrome::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		event->ignore();
		return;
	}
	pressed = true;
	update();
}

void ActivatableListChrome::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		event->ignore();
		return;
	}
	const bool wasPressed = pressed;
	pressed = false;
	update();
	if (wasPressed && rect().contains(event->pos()))
		emit activated();
}

void ActivatableListChrome::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)
	{
		emit activated();
		return;
	}
	QWidget::keyPressEvent(event);
}

void ActivatableListChrome::focusInEvent(QFocusEvent* event)
{
	QWidget::focusInEvent(event);
	update();
}

void ActivatableListChrome::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
	update();
}
