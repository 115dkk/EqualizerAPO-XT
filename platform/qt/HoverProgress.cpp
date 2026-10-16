/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "HoverProgress.h"

#include <utility>

#include <QVariantAnimation>

HoverProgress::HoverProgress(QObject* parent, std::function<void()> onChange)
	: QObject(parent), changed(std::move(onChange))
{
}

double HoverProgress::value() const
{
	return current;
}

void HoverProgress::setFinishedHandler(std::function<void()> onFinished)
{
	finished = std::move(onFinished);
}

void HoverProgress::animateTo(double target, int durationMs)
{
	if (animation == nullptr)
	{
		animation = new QVariantAnimation(this);
		animation->setEasingCurve(QEasingCurve::OutCubic);
		connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& step) {
			current = step.toDouble();
			if (changed)
				changed();
		});
		connect(animation, &QVariantAnimation::finished, this, [this]() {
			if (finished)
				finished();
		});
	}
	// Interruptible: retarget from the current value, never restart from 0.
	// When the duration changes mid-flight, setDuration first re-evaluates
	// the running curve at the elapsed time over the new duration and
	// reports that value, so the retarget starts there instead: a 150 ms
	// fade-in left at 0.51 fades out from 0.64 over 110 ms (measured). The
	// three copies this replaced did the same (audit #422 TD-53 moved the
	// code without changing a frame).
	animation->stop();
	animation->setDuration(durationMs);
	animation->setStartValue(current);
	animation->setEndValue(target);
	animation->start();
}
