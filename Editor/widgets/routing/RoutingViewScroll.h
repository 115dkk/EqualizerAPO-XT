/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The sideways scroll around a routing view. A routing view gives up its
	width on purpose (horizontal policy Ignored, so a wide board cannot push
	the card column out of the window), which left a board wider than its card
	cut off at the card's edge with no way to reach the rest: a Rack or Matrix
	board over a BRIR with many channels. This wrapper gives the view the width
	its board needs and scrolls it sideways when the card is narrower.

	It never scrolls vertically. Its height is the view's height, plus the
	room the bar takes when there is one, so the bar lies below the board and
	not over its last row, and the wheel goes on to the list.
*/

#pragma once

#include <QScrollArea>

class RoutingView;

class RoutingViewScroll : public QScrollArea
{
	Q_OBJECT

public:
	explicit RoutingViewScroll(RoutingView* view, QWidget* parent = nullptr);

	RoutingView* view() const { return routingView; }

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;

private:
	// Sizes the view to its board (never narrower than the viewport) and this
	// area to the view.
	void fit();

	RoutingView* routingView = nullptr;
	bool fitting = false;
};
