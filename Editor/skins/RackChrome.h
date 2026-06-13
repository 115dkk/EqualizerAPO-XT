/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Painted chrome for the "rack" skin ("The amplifier faceplate",
	skeuomorphic 19-inch rack hardware). Every command row is dressed as a
	rack unit: brushed faceplate, rack ears, four corner screws, status LEDs,
	patchbay jacks on Include units and a brass nameplate on VST units. Knobs
	are physical pointer knobs whose scale is printed on the panel around
	them, as real hardware does. Everything is QPainter-drawn - no image
	resources - so the chrome stays DPI independent. Kept out of Skins.cpp so
	the skin class stays a thin registration shim.
*/

#pragma once

#include <QRect>

struct CommandRowInfo;
struct KnobState;
struct SkinTokens;
class QPainter;
class QToolBar;
class QWidget;

namespace RackChrome
{
// Width of the rack-ear zone reserved on both faceplate edges; row content
// is inset past it (see RackSkin::prepareCommandRow).
int earWidth();

// Extra header inset reserved on VST rows for the painted brand nameplate.
int nameplateReserve();

// Faceplate decoration drawn by CommandRowFrame between the QSS background
// and the child widgets.
void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens);

// Skeuomorphic pointer knob with a panel-printed scale.
void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens);

// AR2: dress the shared Include/VST ReferenceCard body as rack hardware. The
// card carries a neutral, per-widget stylesheet that wins over the app QSS
// for its labels and badges, so the rack translation is applied in code from
// RackSkin::prepareCommandRow (rows rebuild on every skin switch, so the one
// call at construction is enough). Include reads as a patch sheet seated in a
// recessed display well; a missing VST reads as an empty pulled-module bay;
// the file/plugin name is the lit engraving, the directory an LCD-mono line,
// the format/ABS/MISSING badges are engraved wireframe tokens (no fill - this
// skin's colour lives only in lamps and engraving) and Locate is a switch-cap
// re-seat affordance. `body` is the IncludeCardEditor / VSTCardEditor widget;
// `isVst` selects the object-name namespace. No-op when no ReferenceCard is
// found.
void dressReferenceCard(QWidget* body, bool isVst, const SkinTokens& tokens);

// The custom title bar as the unit's top panel: brushed sheen and brushing
// lines, machined top/bottom edges, the caption-button block set off by a
// machined groove (the same ear grammar as the cards and the master rail)
// and two hand-tightened rail screws. Drawn by TitleBar::paintEvent between
// the QSS background and the child widgets.
void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens);

// Mount (or refresh) the master-rail chrome on the main toolbar: a painted
// overlay widget (brushed strip, machined edges, end screws, engraved series
// marking and the instant-mode power LED) kept below the toolbar's controls.
// Idempotent - re-running only refreshes the tokens. The overlay hides
// itself when another skin's stylesheet takes over, so no chrome leaks
// across skin switches.
void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens);
}
