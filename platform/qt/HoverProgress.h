/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>

#include <QObject>

class QVariantAnimation;

// The hover feel DeviceSelector's painted controls share (the dialog buttons,
// the troubleshooting disclosure and every device row): a progress from 0 at
// rest to 1 under the pointer, eased OutCubic. A retarget eases on from the
// value reached so far, whatever the new duration, so a pointer that leaves
// halfway fades back from halfway instead of jumping. The owner picks the
// durations and repaints in the change callback; the painters read value().
class HoverProgress : public QObject
{
public:
	HoverProgress(QObject* parent, std::function<void()> onChange);

	double value() const;
	void animateTo(double target, int durationMs);

	// Runs each time an animation reaches its end value, so an owner that
	// keeps one progress per item can drop the faded-out ones (deleteLater:
	// the handler runs inside the animation's own signal).
	void setFinishedHandler(std::function<void()> onFinished);

private:
	QVariantAnimation* animation = nullptr;
	double current = 0.0;
	// Set while animateTo reconfigures the animation, whose reports in that
	// span are not on the new curve.
	bool retargeting = false;
	std::function<void()> changed;
	std::function<void()> finished;
};
