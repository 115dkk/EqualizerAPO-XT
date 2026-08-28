/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

class MainWindow;

namespace SkinSwitchStorm
{
// Runs the hidden field diagnostic against the real window and exits with a
// non-zero status if toolbar health regresses.
void run(MainWindow& window);
}
