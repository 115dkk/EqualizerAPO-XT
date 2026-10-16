/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <cmath>
#include <functional>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QThread>

#include "EditorLogicTestSupport.h"
#include "platform/qt/HoverProgress.h"

namespace
{
// Runs the event loop until the condition holds or the deadline passes, so
// the animation timer can step.
bool waitFor(const std::function<bool()>& condition, int timeoutMs)
{
	QElapsedTimer clock;
	clock.start();
	while (!condition())
	{
		if (clock.elapsed() > timeoutMs)
			return false;
		QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
		QThread::msleep(1);
	}
	return true;
}
}

// DeviceSelector's buttons, troubleshooting disclosure and device rows share
// one hover progress (audit #422 TD-53). What their painters rely on: it
// rests at 0, settles on each target, a retarget at the same duration starts
// from the value reached instead of jumping, an interrupted animation is not
// a finished one, and a completed fade-out ends at exactly 0 (the device list
// drops a row's progress on that).
void testHoverProgressSettlesAndRetargetsInPlace()
{
	QObject owner;
	int changes = 0;
	int finishes = 0;
	HoverProgress* hover = new HoverProgress(&owner, [&changes]() {
		changes++;
	});
	hover->setFinishedHandler([&finishes]() {
		finishes++;
	});
	expectTrue(hover->value() == 0.0, "a new hover progress rests at 0");

	const int durationMs = 1000;
	hover->animateTo(1.0, durationMs);
	requireTrue(waitFor([&finishes]() { return finishes == 1; }, 10000), "the fade-in finishes");
	expectTrue(hover->value() == 1.0, QString("the fade-in settles on 1, got %1").arg(hover->value()));
	expectTrue(changes > 0, "every step reaches the owner's change callback");

	hover->animateTo(0.0, durationMs);
	requireTrue(waitFor([hover]() { return hover->value() < 0.9; }, 10000), "the fade-out starts moving");
	const double reached = hover->value();
	hover->animateTo(1.0, durationMs);
	expectTrue(std::abs(hover->value() - reached) < 1e-9,
		QString("the retarget starts from %1, got %2").arg(reached).arg(hover->value()));
	expectEqual(finishes, 1, "an interrupted fade-out does not count as finished");

	requireTrue(waitFor([&finishes]() { return finishes == 2; }, 10000), "the retargeted fade-in finishes");
	expectTrue(hover->value() == 1.0, QString("the retargeted fade-in settles on 1, got %1").arg(hover->value()));

	hover->animateTo(0.0, durationMs);
	requireTrue(waitFor([&finishes]() { return finishes == 3; }, 10000), "the fade-out finishes");
	expectTrue(hover->value() == 0.0, QString("a finished fade-out ends at exactly 0, got %1").arg(hover->value()));
}
