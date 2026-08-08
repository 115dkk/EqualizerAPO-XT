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

// Entry point behind --skin-switch-test: the live skin-switch robustness
// gate. Replays MainWindow::skinSelected's exact sequence (clearRows ->
// applySkin -> updateGuis) over a large synthetic config for every skin x
// dark/light, several rounds, and fails on a crash, a wrong resulting skin
// id, a stale title-bar caption icon, or a pathologically slow switch. Field
// history: skin switches have crashed machines, cost seconds per switch
// before the clear-first fix, and left the caption buttons dark-on-dark;
// this keeps those regression classes out of CI-green.
int runSwitchTest(const QStringList& arguments);

// Entry point behind --card-move-test: the card drag-move latency gate.
// Commits a one-card move (down one row, then back) through
// FilterTable::moveRows - the internal drag-and-drop commit path - on a
// 100+ row document for every skin x dark/light, and fails on a wrong
// resulting document order, a lost selection, a row-widget mismatch or a
// move slower than EAPO_MOVE_LIMIT_MS (EAPO_MOVE_WARN_MS only logs). Field
// history: one card move cost 5-6 s on a fast desktop while the move
// rebuilt every card row.
int runCardMoveTest(const QStringList& arguments);
}
