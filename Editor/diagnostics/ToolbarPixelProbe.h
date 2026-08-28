/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QImage>

namespace ToolbarPixelProbe
{
// A healthy toolbar contains controls, labels, and separators. If at least
// 99% of sampled pixels match its corner, paint-only chrome probably covered
// the control train.
inline bool renderIsBlank(const QImage& image)
{
	if (image.isNull() || image.width() < 10 || image.height() < 4)
		return true;
	const QRgb corner = image.pixel(1, 1);
	qint64 same = 0;
	qint64 total = 0;
	for (int y = 0; y < image.height(); y += 2)
	{
		for (int x = 0; x < image.width(); x += 2)
		{
			total++;
			if (image.pixel(x, y) == corner)
				same++;
		}
	}
	return same * 100 >= total * 99;
}
}
