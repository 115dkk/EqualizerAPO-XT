/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Animation durations that follow the system's reduced-motion setting. On
	Windows that is Settings > Accessibility > Visual effects > Animation
	effects (SPI_GETCLIENTAREAANIMATION), which Qt reports from 6.12 on as
	QAccessibilityHints::motionPreference. Read at every animation start, so
	switching the setting applies to the next animation without a restart.
*/

#pragma once

#include <QAccessibilityHints>
#include <QGuiApplication>
#include <QStyleHints>

namespace MotionPreference
{
inline bool reduced()
{
	const QStyleHints* hints = QGuiApplication::styleHints();
	const QAccessibilityHints* accessibility = hints != nullptr ? hints->accessibility() : nullptr;
	return accessibility != nullptr && accessibility->motionPreference() == Qt::MotionPreference::ReducedMotion;
}

// The requested duration, or 0 when the user asked for less motion: a
// zero-length animation reports its end value at once and finishes, so the
// owners' valueChanged/finished handling stays the same.
inline int duration(int requestedMs)
{
	return reduced() ? 0 : requestedMs;
}
}
