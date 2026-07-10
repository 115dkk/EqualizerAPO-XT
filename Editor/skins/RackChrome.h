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
struct GraphicEQPlotState;
struct KnobState;
struct ListChromeState;
struct SkinTokens;
class QPainter;
class QToolBar;

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

// The If-block scope as a RELAY-SWITCHED POWER BUS running down the gutter
// (gate issue #179, variant A): an amber bus bar per scope level, the relay
// feeding the lane from the If unit's bottom edge, changeover contact blocks
// on ElseIf/Else, a terminator cap on EndIf, and tap stubs with pilot lamps
// into every powered unit. The analysis load facts drive the lamps - branch
// taken = green, false/dead branch = dark, evaluation error = danger, no
// analysis yet = unlit dome - and a line a false branch swallowed dims its
// innermost bus segment (a de-energized run, not an alarm). Outer
// channel-group levels keep a muted dotted rail. Returns false for rows
// outside any If scope so the shared channel rail stays in charge there.
// Drawn for FilterCardRow via ISkin::paintScopeGutter.
bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens);

// The trailing "add card" row as an EMPTY RACK BAY: the blank panel is
// missing, so the opening shows the rack's dark interior, the mounting
// rails with their empty bolt holes, and a stencilled EMPTY BAY marking.
// Hover pre-heats the opening amber and the stencil answers INSTALL
// MODULE. Drawn for AddCardRow via ISkin::paintAddRow.
void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens);

// The first-boundary insertion seam as a service slot's amber heat line:
// strokes only (groove shadow + amber line + slot ticks), painted only
// while hovered/pressed - at rest the seam does not exist.
void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens);

// The GraphicEQ card's response plot as the unit's OSCILLOSCOPE DISPLAY: a
// dark phosphor-glass well in BOTH finishes (the display law), seated in a
// recessed bezel (shadowed top edge, lit lower lip). The graticule sits in
// the scope-grid family, axis figures are etched in dim segment ink, the
// response trace is green phosphor whose glow is faked by stroke
// overpainting (no graphics effects), and the nodes are glowing adjustment
// dots (rest dome < hover pre-heat < selected lit + collar ring; the
// keyboard target wears the amber service ring). Band-locked layouts read
// as segmented level ladders. A powered-down unit dims the segments but
// keeps the glass. Drawn for GraphicEQPlotWidget via
// ISkin::paintGraphicEqPlot.
void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens);

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
