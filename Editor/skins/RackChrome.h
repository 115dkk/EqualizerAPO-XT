/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Painted chrome for the "rack" skin ("The amplifier faceplate").
	Constitution: docs/skins/rack.md. The skin class (RackSkin.cpp) stays a
	thin registration shim; the drawing lives here.
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

// The If-block scope as a RELAY-SWITCHED POWER BUS running down the gutter:
// an amber bus bar per scope level, contact blocks on ElseIf/Else, a
// terminator cap on EndIf, and tap stubs with pilot lamps into every powered
// unit. Returns false for rows outside any If scope so the shared channel
// rail stays in charge there. Drawn for FilterCardRow via
// ISkin::paintScopeGutter.
bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens);

// The trailing "add card" row as an EMPTY RACK BAY. Drawn for AddCardRow
// via ISkin::paintAddRow.
void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens);

// The first-boundary insertion seam as a service slot's amber heat line:
// strokes only (groove shadow + amber line + slot ticks), painted only
// while hovered/pressed - at rest the seam does not exist.
void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens);

// The GraphicEQ card's response plot as the unit's OSCILLOSCOPE DISPLAY: a
// dark phosphor-glass well in BOTH finishes (the display law). Drawn for
// GraphicEQPlotWidget via ISkin::paintGraphicEqPlot.
void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens);

// The custom title bar as the unit's top panel: brushed sheen, machined
// edges, the caption-button ear and two rail screws. Drawn by
// TitleBar::paintEvent between the QSS background and the child widgets.
void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens);

// Mount (or refresh) the master-rail chrome on the main toolbar: a painted
// overlay widget (brushed strip, machined edges, end screws, engraved series
// marking and the instant-mode power LED) kept below the toolbar's controls.
// Idempotent - re-running only refreshes the tokens. The overlay hides
// itself when another skin's stylesheet takes over, so no chrome leaks
// across skin switches.
void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens);
}
