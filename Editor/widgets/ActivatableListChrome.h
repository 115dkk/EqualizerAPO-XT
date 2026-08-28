/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QWidget>

class QEnterEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;

// Shared pointer, keyboard, and focus state machine for list insertion
// affordances. Subclasses own only their size and painting grammar.
class ActivatableListChrome : public QWidget
{
	Q_OBJECT

public:
	explicit ActivatableListChrome(QWidget* parent = nullptr);

signals:
	void activated();

protected:
	bool isHovered() const;
	bool isPressed() const;

	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;

private:
	bool hovered = false;
	bool pressed = false;
};
