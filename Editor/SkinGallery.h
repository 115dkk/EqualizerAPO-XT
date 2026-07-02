/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Offscreen screenshot gallery for the skin program. For each requested skin
	and dark/light mode it renders representative filter card rows (a simple
	filter, a shelf filter with its three knobs, an Include row, a VST row) in
	normal, hover-equivalent and disabled states, and writes deterministic
	PNGs named <skin>_<dark|light>_<row>_<state>.png to a target directory.

	Runs headless: invoke the Editor with QT_QPA_PLATFORM=offscreen and
	--skin-gallery <outDir> [--skin-gallery-skins id,id,...]. Used by the skin
	agents and CI to prove appearance-preserving changes (pixel-identical
	before/after) and to build judging contact sheets.
*/

#pragma once

#include <QStringList>

namespace SkinGallery
{
// Entry point behind the Editor's --skin-gallery flag. arguments are the full
// application arguments. Returns a process exit code: 0 when every PNG was
// written, 1 when rendering failed, 2 on bad usage.
int run(const QStringList& arguments);
}
