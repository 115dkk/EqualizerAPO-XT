/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>
#include <memory>

#include <QtGui/qwindowdefs.h>

#include "vst/VSTPluginLibrary.h"

namespace PluginLoadQueue
{
// Call these on the UI thread while a QCoreApplication exists.
void enqueue(const std::shared_ptr<VSTPluginLibrary>& library,
	std::function<void(int result)> onDoneOnUiThread);
bool isPending(const VSTPluginLibrary* library);
int pendingCount();
// A plug-in's DLL entry point holds the process loader lock for the whole
// load, and the UI thread stops whenever it waits for a thread that has yet to
// start or for a DLL that has yet to load. These do that work before the
// first load, on the two ways the UI thread was seen to stop during a 4 s
// fixture load. main calls them in this order, once per QApplication:
// prepareUiThread right after creating it, holdLoads before the MainWindow
// (whose constructor reopens tabs and queues their plug-ins), then
// startUiAutomation and releaseLoads once the window is shown.
//
// The raster paint engine splits a large fill across Qt's GUI thread pool and
// waits for it (QT_THREAD_PARALLEL_FILLS in qdrawhelper.cpp). The pool starts
// its threads on demand and retires them after 30 s idle; this starts them
// all and keeps them.
void prepareUiThread();
// The loader takes nothing from the queue until releaseLoads.
void holdLoads();
void releaseLoads();
// The first request from a UI Automation client (an input or screen-reading
// tool asking for the window) starts UIA's own thread, and unmarshalling the
// client's calls loads COM's OneCore proxy/stub DLL. Measured with two UIA
// clients running; another client may need other DLLs.
void startUiAutomation(WId shownWindow);
// Drops queued loads, waits for the one in progress and ends the loader
// thread. main calls it once the last QApplication is gone, so no plug-in
// code or log write runs during static destruction.
void shutdown();
}
