/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Works around QTBUG-151308 in Qt 6.12.0 (fixed for 6.12.1). When a style
	sheet rule gives a spin box a box or a non-native border, the style sheet
	style puts the spin box's line edit at x = 0 instead of at the rule's
	contents rect, so the text slides under the left border and padding. A
	spin box without buttons gets a 1px wide edit field, so its value does
	not show at all: every value cell in the cards rendered empty. An
	application-wide event filter puts the line edit back where Qt 6.10 and
	6.12.1 put it. It is installed on Qt 6.12.0 only; remove this file with
	the move to 6.12.1.
*/

#pragma once

class QApplication;

namespace SpinBoxEditFieldFix
{
void installIfAffected(QApplication& application);
}
