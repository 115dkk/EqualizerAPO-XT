/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "HoverProgress.h"

#include <utility>

#include <QVariantAnimation>

#include "MotionPreference.h"

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
			if (retargeting)
				return;
			current = step.toDouble();
			if (changed)
				changed();
		});
		connect(animation, &QVariantAnimation::finished, this, [this]() {
			if (finished)
				finished();
		});
	}
	// Interruptible: an interrupted animation restarts from the value it had
	// reached, never from 0. The value is read before the animation is
	// touched, and the reconfiguration's own reports are ignored: setDuration,
	// setStartValue and setEndValue each re-evaluate the old curve at the
	// elapsed time and report that value, and with a new duration it lies
	// further along (a 150 ms fade-in left at 0.51 reported 0.64 to a 110 ms
	// fade-out, which then started there). start() reports the start value,
	// so the owner sees the curve continue from where it stopped.
	const double from = current;
	retargeting = true;
	animation->stop();
	animation->setDuration(MotionPreference::duration(durationMs));
	animation->setStartValue(from);
	animation->setEndValue(target);
	retargeting = false;
	animation->start();
}
