/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

/*
	Whether this process keeps every VST plugin module it loads until the
	process ends.

	Some plugins do seconds of work in their DLL entry point. A copy-protected
	plugin measured at 3 to 18 s inside LoadLibraryW holds the process loader
	lock all that time, so in audiodg.exe every thread creation, for every
	audio endpoint, waits with it. The engine used to unload a plugin with the
	last filter that used it, which happens whenever a stream ends and its
	pipeline is torn down, so the next stream start paid the whole load again.
	With the policy on, the engine's plugin load site
	(VSTPluginFilterFactory) hands each loaded library to a registry that is
	never emptied: the first load still costs what it costs, and every later
	stream start reuses the module.

	The cost: a retained plugin file stays mapped, so a plugin installer
	cannot overwrite it until the audio service (or the engine host) restarts.
	There is no idle timeout, because every release would re-arm the stall at
	a moment nobody can predict.

	The APO DLL in audiodg and EqualizerAPOHost turn it on, and so does the
	Editor: it rebuilds every row on a skin switch, undo or device change,
	and each rebuild used to reload every plugin in the config. There the
	cost is a plugin file that stays in use until the Editor closes. The
	in-process ASIO processor and the tests keep the old behaviour.
	The two functions are declared apart from VSTPluginLibrary.h so those
	processes need not see the VST3 SDK headers to set it. The implementation
	lives in VSTPluginLibrary.cpp.
*/
namespace PluginResidency
{
// Setting a flag only, so it is safe to call from DllMain.
void keepLoadedForProcessLifetime(bool keep);

// True once a plugin library has been retained. The APO's DllCanUnloadNow
// refuses while this holds: the retained objects run code from that DLL.
bool holdsLoadedModules();
}
