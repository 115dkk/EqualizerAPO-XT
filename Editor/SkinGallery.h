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

class MainWindow;

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

// Entry point behind --card-selection-test: the pointer-selection regression
// gate. It verifies that a plain header click moves both the model focus and
// the rendered card state, then that clicking an interactive control inside a
// different card collapses a prior multi-selection onto that card.
int runCardSelectionTest(const QStringList& arguments);

// Entry point behind --selftest-vst: the VSTPlugin store()/parse round-trip
// gate (opening and saving a line must never lose chunk data, parameters or
// the bus contract). Lived inline in main.cpp before audit #275 B7.
int runVstRoundTripSelfTest();

// Second half of --selftest-vst: the channel-fill menus of a VST row must
// offer the channels selected at that row in both presentations, and follow
// a Channel row above it. Field report (v2.41.2): under the heritage
// (LegacyRows) presentation every fill combo listed only "Silence (-)" while
// the card path offered every device channel, so a stereo-to-5.1 upmixer
// could not be fed from the legacy rows at all.
int runVstFillSelfTest();

// Entry point behind --power-toggle-test: the header power toggle must
// round-trip a row's text (off adds "# ", on removes it, nothing else
// changes) and the re-enabled row must come back as its real editor, not
// a raw-text fallback. Field report (v2.42.x, rack): powering a gain-less
// biquad on left the card collapsed and its body a raw fragment.
int runPowerToggleTest();

// Entry point behind --scroll-bench: per-skin wall time of wheel-sized
// scroll steps on a maximized-width viewport, for the "rack lags when
// maximized" report. Diagnostic, not a CI gate.
int runScrollBench();

// Entry point behind --analysis-layout-test: arms the timed probe over the
// live MainWindow (dock geometry, right/bottom relayout, restore) and later
// exits the event loop with the verdict. Returns false when the required
// dock is missing, in which case the caller exits 1 immediately.
bool armAnalysisLayoutProbe(MainWindow& window, const QString& screenshotPath);
}
