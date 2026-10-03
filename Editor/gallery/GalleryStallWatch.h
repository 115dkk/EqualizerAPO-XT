/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

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
