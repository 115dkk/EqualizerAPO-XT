/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QLocale>
#include <QString>

// EditableValue displays and seeds C-locale decimals. Preserve that meaning
// before accepting a user's system-locale decimal syntax as a convenience.
bool parseEditableValueText(const QString& text, const QLocale& fallbackLocale, double* value);
