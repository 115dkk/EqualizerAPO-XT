/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	What the offscreen probes (Editor/gallery) and the diagnostic sessions
	(Editor/diagnostics) may reach inside the live MainWindow beyond its
	public interface. They used to be friends with access to all of it
	(audit #422 TD-59); this names the few things they actually use. A
	nested class reaches MainWindow's private members without a friend
	declaration.
*/

#pragma once

// Probe's declaration now includes its stored state and therefore lives with
// MainWindow. Keep this include for the existing probe clients (audit #467 TD-75).
#include "Editor/MainWindow.h"
