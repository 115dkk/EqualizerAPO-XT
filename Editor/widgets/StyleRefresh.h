/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The one stylesheet refresh every restyled widget goes through. It lives
	outside the skin headers so that non-skin sources (MainWindow parts, the
	shared card editors, CommandRowFrame) can use it without including a skin
	header; SkinPaint.h includes it for the skin TUs (audit #467 TD-49).
*/

#pragma once

#include <QStyle>
#include <QWidget>

// Re-evaluate a widget's stylesheet after one of its dynamic properties
// changed. Rules keyed on a widget's own property re-resolve only when it is
// polished again, and the skin views' base classes repolish only the view
// itself, not the children whose properties applyState sets.
inline void repolish(QWidget* widget)
{
	if (widget == nullptr)
		return;

	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
	widget->update();
}
