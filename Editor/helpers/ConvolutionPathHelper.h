/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

class ConvolutionPathHelper
{
public:
	static QString absolutePathForConfig(const QString& configPath, const QString& path);
	static QString displayPathForSelection(const QString& configPath, const QString& selectedPath);
	static bool relativePathLooksContainedLexically(const QString& relativePath);
};
