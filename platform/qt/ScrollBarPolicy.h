/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QAbstractScrollArea>
#include <QTimer>

// Works around Qt 6 opening an item view with a horizontal scroll bar although
// the dialog's size already fits its contents: the bar stays off until the
// first event-loop turn, then goes back to as-needed. Call after the dialog
// has its size. The view is the timer's context, so a view destroyed before
// that turn cancels the callback instead of leaving it a dangling pointer.
inline void deferScrollBarPolicy(QAbstractScrollArea* view)
{
	view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	QTimer::singleShot(0, view, [view]() {
		view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	});
}
