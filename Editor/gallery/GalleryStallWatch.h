/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <memory>

#include <QString>

// RAII around one timed GUI operation of a gate. When the operation runs past
// the threshold, a sampler thread records GUI-thread call stacks until it ends;
// the destructor prints a report with qWarning when the threshold was crossed.
class GalleryStallWatch
{
public:
	GalleryStallWatch(const char* gate, const QString& operation, int thresholdMs);
	~GalleryStallWatch();
	GalleryStallWatch(const GalleryStallWatch&) = delete;
	GalleryStallWatch& operator=(const GalleryStallWatch&) = delete;

private:
	const char* gate;
	QString operation;
	bool armed = false;
};

// The machine a whole gate run shared: wall and CPU time, how busy the
// system was, and the other processes that used the most CPU. Printed once,
// stalled or not, so every CI run records what else ran on the runner.
class GalleryLoadSummary
{
public:
	explicit GalleryLoadSummary(const char* gate);
	~GalleryLoadSummary();
	GalleryLoadSummary(const GalleryLoadSummary&) = delete;
	GalleryLoadSummary& operator=(const GalleryLoadSummary&) = delete;

	void report() const;

private:
	struct State;
	const char* gate;
	std::unique_ptr<State> state;
};
