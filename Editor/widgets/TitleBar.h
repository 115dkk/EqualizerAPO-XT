/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Custom window title bar for the frameless main window. The native Windows
	caption clashed with every skin (and the menu bar under it), so the window
	draws its own: app title text and the minimize / maximize / close buttons,
	all QSS-targetable (#AppTitleBar, #TitleBarText, #TitleBarMin, #TitleBarMax,
	#TitleBarClose) and re-iconed through the skin layer. The frameless window
	plumbing (WM_NCCALCSIZE / WM_NCHITTEST) lives in MainWindow::nativeEvent;
	this widget is presentation plus button wiring only. A registry escape
	hatch (interface/nativeTitleBar) restores the native caption for machines
	where custom chrome misbehaves.
*/

#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

class TitleBar : public QWidget
{
	Q_OBJECT

public:
	// dialogMode drops the minimize/maximize buttons and the double-click
	// maximize gesture: dialog captions carry only the title and the close
	// button, per the platform convention. The QSS names and the painted
	// skin chrome stay identical to the main window's strip.
	explicit TitleBar(QWidget* window, QWidget* parent = nullptr, bool dialogMode = false);

	// True when the given point (in this widget's coordinates) sits on the
	// draggable caption area (i.e. not on one of the buttons). Used by the
	// host's WM_NCHITTEST handler.
	bool isCaptionPoint(const QPoint& point) const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	// QSS background first, then the active skin's painted chrome
	// (ISkin::paintTitleBarChrome), then children.
	void paintEvent(QPaintEvent* event) override;

private:
	// Re-tint the caption button icons from the active skin's tokens; wired
	// to SkinManager::skinChanged in the constructor so every switch path
	// (menu, shortcut, preferences) keeps the glyphs on the skin's ink.
	void applySkinIcons();
	void updateMaximizeButton();

	QWidget* hostWindow = nullptr;
	QLabel* titleLabel = nullptr;
	QToolButton* minimizeButton = nullptr;
	QToolButton* maximizeButton = nullptr;
	QToolButton* closeButton = nullptr;
	bool dialogMode = false;
};
