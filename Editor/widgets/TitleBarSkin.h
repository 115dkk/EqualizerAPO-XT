/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	What the caption strip (TitleBar) takes from the program's skin layer:
	the ink for its glyphs and the painted chrome over its QSS ground, and a
	signal when either changes. The Editor installs one that reads
	SkinManager; the Device Selector, which has no SkinManager, installs one
	that reads its own skin painter. That way the strip, the dialog chrome
	(DialogChrome) and the frame hit test are one piece of code in both
	programs. With none installed the strip uses the palette's text colour
	and paints no chrome.
*/

#pragma once

#include <QColor>
#include <QObject>

class QPainter;
class QRect;

class TitleBarSkin : public QObject
{
	Q_OBJECT

public:
	using QObject::QObject;

	// The installed skin, or nullptr. Not owned: the program that installs
	// one keeps it alive while captions exist.
	static TitleBarSkin* current();
	static void install(TitleBarSkin* skin);

	virtual QColor ink() const = 0;
	virtual void paintChrome(QPainter& painter, const QRect& rect) const = 0;

signals:
	// The skin or its mode changed; captions re-tint their glyphs.
	void changed();
};
