/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Real-window regression probe for the custom frame's hit testing
	(--frame-hittest-test). It reproduces the condition behind "the title
	bar no longer drags after a skin switch": a widget deep in the rows asks
	for its winId(), as the VST panel embedded in a card does, so Qt turns
	the chrome host into a native child window over the caption; then a
	skin switch rebuilds the rows. Every frame point (caption, top and side
	edges) must then be answered HTTRANSPARENT by the child that covers it
	and with the frame code by the top level, while the caption buttons, the
	menu bar and the content keep HTCLIENT. Needs the windows platform
	plugin (WM_NCHITTEST is a Win32 message); exits 0 on pass, 1 on failure.
	With EAPO_FRAME_PROBE_LINGER=1 the window stays open afterwards for a
	hand (or scripted) drag test.
*/

#pragma once

class MainWindow;

namespace FrameHitTestProbe
{
bool arm(MainWindow& window);
}
