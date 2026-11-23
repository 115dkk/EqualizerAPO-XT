/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The skinned caption on the Device Selector's own windows (the device list
	and the installation check). The strip, the dialog chrome and the frame
	hit test are the Editor's (TitleBar, DialogChrome, WindowFrameHitTest),
	painted through this program's skin painter, so these windows wear the
	caption the Editor's dialogs wear in the same skin. The native Windows
	caption stays in the heritage look and when the Editor's
	interface/nativeTitleBar setting asks for it, as it does in the Editor.
	Message boxes keep the native caption in both programs.
*/

#pragma once

class QDialog;

namespace DeviceCaption
{
// Installs the caption skin and switches mounting on or off. main() calls it
// once it has dressed the process; the shot harness turns it on.
void enable(bool on);
bool enabled();

// Mounts the caption on a top-level dialog that has its layout, when
// enabled. Call it after the dialog's last window-flag change: the chrome
// creates the native window.
void mount(QDialog* dialog);
}
