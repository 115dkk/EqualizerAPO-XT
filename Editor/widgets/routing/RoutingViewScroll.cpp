/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "RoutingViewScroll.h"

#include <QEvent>
#include <QLayout>
#include <QScrollBar>
#include <QWheelEvent>

#include "IRoutingRenderer.h"

RoutingViewScroll::RoutingViewScroll(RoutingView* view, QWidget* parent)
	: QScrollArea(parent), routingView(view)
{
	setObjectName(QStringLiteral("RoutingViewScroll"));
	setFrameShape(QFrame::NoFrame);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	// The view keeps the size fit() gives it: with widgetResizable its
	// Ignored policy would hand the board the viewport's width again.
	setWidgetResizable(false);
	setFocusPolicy(Qt::NoFocus);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	setMinimumSize(0, 0);
	viewport()->setAutoFillBackground(false);
	setStyleSheet(QStringLiteral(
		"QScrollArea#RoutingViewScroll, QScrollArea#RoutingViewScroll > QWidget#qt_scrollarea_viewport { background: transparent; }"));

	setWidget(view);
	view->installEventFilter(this);
	connect(horizontalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
		fit();
	});
	fit();
}

QSize RoutingViewScroll::sizeHint() const
{
	return QSize(routingView != nullptr ? routingView->sizeHint().width() : 0, height());
}

QSize RoutingViewScroll::minimumSizeHint() const
{
	return QSize(0, height());
}

void RoutingViewScroll::fit()
{
	if (fitting || routingView == nullptr)
		return;
	fitting = true;

	const QSize wanted = routingView->sizeHint();
	const QSize board(qMax(viewport()->width(), wanted.width()), wanted.height());
	if (routingView->size() != board)
		routingView->resize(board);

	const QScrollBar* across = horizontalScrollBar();
	const int barRoom = across->maximum() > across->minimum() ? across->sizeHint().height() : 0;
	const int tall = board.height() + barRoom;
	if (minimumHeight() != tall || maximumHeight() != tall)
	{
		setFixedHeight(tall);
		// The card above lays this out through several nested layouts whose
		// cached hints do not notice a fixed height changing on its own
		// (IRoutingRenderer.h, syncSizeToHint).
		for (QWidget* ancestor = parentWidget(); ancestor != nullptr; ancestor = ancestor->parentWidget())
		{
			if (ancestor->layout() != nullptr)
				ancestor->layout()->invalidate();
			ancestor->updateGeometry();
		}
	}
	fitting = false;
}

bool RoutingViewScroll::eventFilter(QObject* watched, QEvent* event)
{
	// The view resizes itself to its hint when its board changes (a fold
	// opened, a channel added); that is the moment the board's size is new.
	if (watched == routingView && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest))
		fit();
	return QScrollArea::eventFilter(watched, event);
}

void RoutingViewScroll::resizeEvent(QResizeEvent* event)
{
	QScrollArea::resizeEvent(event);
	fit();
}

void RoutingViewScroll::wheelEvent(QWheelEvent* event)
{
	// Sideways with Shift, or with a wheel that tilts; everything else is the
	// list's to scroll.
	QScrollBar* across = horizontalScrollBar();
	const bool sideways = event->angleDelta().x() != 0 || event->modifiers().testFlag(Qt::ShiftModifier);
	if (sideways && across->maximum() > across->minimum())
	{
		const int delta = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y();
		across->setValue(across->value() - delta);
		event->accept();
		return;
	}
	event->ignore();
}
