/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A skin is a self-contained visual identity. Beyond colour tokens it decides
	which QSS sheet to load and which Copy routing renderer to inject, so that
	each skin can present the same configuration with a genuinely different
	philosophy rather than only different colours. SkinManager owns the active
	ISkin and delegates to it; new skins are added by implementing this
	interface and registering an instance (see Skins.cpp).
*/

#pragma once

#include <QColor>
#include <QPolygonF>
#include <QRect>
#include <QSet>
#include <QString>
#include <QVector>

#include "Editor/SkinTokens.h"
#include "Editor/analysis/AnalysisMetric.h"

class FilterPickerView;
class IRoutingRenderer;
class QFileDialog;
class QPainter;
class QToolBar;
class QWidget;
class ReferenceCardView;
class SubwooferRoutingCardView;

// Identifies a command row for per-command-type chrome decisions.
struct CommandRowInfo
{
	// FilterCardModel descriptor type ("biquad", "include", "vst", "copy", ...).
	QString type;
	// Lower-cased command token ("filter 1", "include", "vstplugin", ...);
	// matches the "filterKind" dynamic property the card row already sets.
	QString command;
	// True when the consulting widget belongs to the frozen legacy row path
	// (FilterTableRow and the .ui-based filter GUIs it hosts).
	bool legacyRow = false;
	bool enabled = true;
	bool selected = false;
	bool focused = false;
	// True while the cursor is over the row. Populated at paint time by
	// CommandRowFrame for ISkin::paintCardChrome; the construction-time hooks
	// (prepareCommandRow) always see false.
	bool hovered = false;
	int depth = 0;
	// Number of If scopes the row lives inside (FilterCardRowScope::logic):
	// ElseIf/Else/EndIf rows count the scope they branch/close, the If head
	// counts only the scopes above its own. Lets a skin draw If-block rails
	// through branch rows and terminate them on the EndIf row, independent of
	// the channel-group indent that depth carries.
	int logicDepth = 0;
	// LANE GEOMETRY, resolved by the row widget that owns the layout.
	//
	// Four skins used to recompute the same three expressions from depth,
	// logicDepth and tokens.channelGroupIndent - the card's left edge, the centre
	// of an indent band, and the "branch and tail rows mount one unit deeper"
	// rule. The arithmetic is not a design decision, and having it in four places
	// meant a change to the indent could move the card face without moving the
	// lanes drawn beside it. A skin now answers what a lane looks like; where it
	// is comes with the row.
	//
	// laneUnit is one indent band's width, laneCount how many bands are drawn to
	// the left of the card face, and cardLeft the x the face starts at. All in
	// row-widget coordinates, which is the space paintScopeGutter paints in.
	int laneUnit = 0;
	int laneCount = 0;
	int cardLeft = 0;

	// Centre of the given indent band, 0-based from the outermost. What a rail or
	// a scope lane is drawn on.
	int laneCenter(int level) const
	{
		return cardLeft - (laneCount - level) * laneUnit + laneUnit / 2;
	}
	// Facts from the analysis engine's most recent configuration load
	// (ConfigLoadTrace), advisory only - they go stale between an edit and
	// the next analysis run. branchState applies to If-family rows:
	// -1 = unknown (no analysis yet), 0 = condition false / dead branch,
	// 1 = branch taken, 2 = not evaluated (chain already satisfied),
	// 3 = evaluation error.
	int branchState = -1;
	// True when a false branch swallowed this line on the last load.
	bool lineSkipped = false;
	// Eval result or the substituted inline-expression parameter text
	// (empty when unknown); valueError marks a parser failure.
	QString evalText;
	bool valueError = false;
	// Why the last analysis run could not use this line's parameters, straight
	// from the factory that owns the command, or empty when it was fine. A row
	// with this set is one the engine skipped, which until now looked exactly
	// like a note somebody typed.
	//
	// No skin paints it yet: a per-skin treatment for it is a design decision and
	// belongs in a round with the gallery gate. The card carries it as a tooltip
	// in the meantime, which is not a skin surface.
	QString parseError;
	// True when the line's parameters carry inline `expression` segments, so
	// its numbers are decided at load time. Rows without a dynamic-capable
	// editor host the shared raw body; skins extend their raw-body styling
	// to these rows through this flag.
	bool dynamicLine = false;
	// A short fact the body editor posts for the chrome once it has loaded
	// its subject: the VST card posts the loaded ABI ("VST3"/"VST2") through
	// the row's rowFormatTag dynamic property, and CommandRowFrame samples
	// it at paint time (like hovered). Empty until the subject loaded.
	QString formatTag;
};

// One value object owns both halves of the type-badge treatment.  Keeping the
// stylesheet and pictogram ink together prevents a skin from changing one
// without the other.
struct BadgeTreatment
{
	QString qss;
	QColor ink;
};

// Interactive state for the list-level add/insert chrome: the trailing
// "add card" row (AddCardRow) and the first-boundary insertion seam
// (FilterInsertSeam). The widgets own all input handling; the skin only
// paints. label carries the widget's translated caption ("Add filter"); a
// skin may draw it in its own register or replace it with its own grammar.
struct ListChromeState
{
	bool hovered = false;
	bool pressed = false;
	bool focused = false;
	QString label;
};

// Snapshot of the GraphicEQ card's response plot handed to
// ISkin::paintGraphicEqPlot. GraphicEQPlotWidget owns the model and every
// input gesture (node drag, add/remove, selection, dB zoom/pan, keyboard);
// the skin owns every pixel. All positions are widget-local pixels, already
// mapped from Hz/dB, so a skin renders geometry without redoing the math.
struct GraphicEQPlotState
{
	// Full widget rect and the inner data area (labels live in the margins).
	QRect rect;
	QRectF plotRect;
	bool enabled = true;
	bool focused = false;
	// True in the 15/31-band layouts: node frequencies are fixed, and the
	// response reads as levels on fixed bands (skins may draw stems/bars).
	bool bandLocked = false;
	// The response curve sampled across plotRect, and the y of 0 dB (may lie
	// outside plotRect when the frame is panned away from it).
	QPolygonF curve;
	double zeroY = 0;
	// Node handles in px, in node order; selection/hover index into this.
	QVector<QPointF> nodePositions;
	QSet<int> selectedNodes;
	int hoveredNode = -1;
	int focusedNode = -1;
	// Grid with prepared labels ("1k", "+6"); minor lines carry no label.
	struct GridLine
	{
		double pos = 0;
		QString label;
		bool major = false;
	};
	QVector<GridLine> vertical;
	QVector<GridLine> horizontal;
	// Cursor readout while the pointer is inside plotRect.
	bool cursorValid = false;
	QPointF cursor;
	QString cursorText;
};

// Snapshot of the analysis dock's response graph handed to
// ISkin::paintAnalysisGraph. EqGraphView owns the sampling, the axis fit and
// the cursor tracking; the skin owns every pixel. All positions are
// widget-local pixels, already mapped.
struct AnalysisGraphState
{
	// Full widget rect and the inner data area (labels live in the margins).
	QRect rect;
	QRectF plotRect;
	// Which quantity is on screen. Magnitude is the default, and the only one
	// where rising above zero is a danger state - see clipping below.
	AnalysisMetric metric = AnalysisMetric::MagnitudeDb;
	// The whole config's response sampled per px across plotRect, split
	// wherever the metric has no value at all. Magnitude always arrives as a
	// single segment; phase and group delay break inside a null, where a
	// reading would have to be invented. Never join two segments - a bridging
	// line claims the response passed through values it never had.
	QVector<QPolygonF> curves;
	// y of the metric's zero, and whether that zero lands inside plotRect.
	// Magnitude fits symmetrically so its zero is always visible; a group delay
	// that never goes negative can push it to the very edge.
	double zeroY = 0;
	bool zeroVisible = false;
	// The fitted value range, in the metric's own unit.
	double minimum = 0;
	double maximum = 0;
	// "dB", "deg", "ms". Present so a skin can compose its own typography
	// (upper case, an abbreviation) without knowing which metric is showing.
	QString unit;
	// Magnitude rose above 0 dB - the response can clip. Universal danger
	// semantics; skins should make the overshoot readable. Never set for phase
	// or group delay, where a positive value is ordinary.
	bool clipping = false;
	// Grid with prepared labels ("100", "1k", "+6"); minor lines carry none.
	struct GridLine
	{
		double pos = 0;
		QString label;
		bool major = false;
	};
	QVector<GridLine> vertical;
	QVector<GridLine> horizontal;
	// Footer caption, already formatted ("All - 48000 Hz"; channel only
	// while no analysis ran yet).
	QString channelText;
	// Prepared axis captions. A skin prints these rather than formatting a
	// number and appending a unit, so a new metric does not mean editing five
	// skins again. topValueText/bottomValueText caption the two ends of the
	// value axis ("+12 dB", "-12 dB"), spanValueText combines them
	// ("+12 / -12 dB"), and the footer texts caption the frequency axis ends
	// ("20 Hz", "20 kHz") - which are not always 20 Hz and 20 kHz, because the
	// upper end stops at Nyquist.
	QString topValueText;
	QString bottomValueText;
	QString spanValueText;
	QString leftFooterText;
	QString rightFooterText;
	// Pointer-driven readout: cursor position inside plotRect, the y of the
	// response under it, and the prepared "1.2 kHz  -3.4 dB" text.
	bool cursorValid = false;
	QPointF cursor;
	double curveYAtCursor = 0;
	QString cursorText;
	// Widget hover progress, 0..1, animated (150ms in / 110ms out).
	double hover = 0.0;
};

// Snapshot of a SegmentedControl handed to ISkin::paintSegmentedControl. The
// widget owns every bit of input handling and state; the skin only draws, which
// keeps repaints cheap and lets a theme switch restyle everything with one
// update() broadcast.
struct SegmentedControlState
{
	QRect rect;
	QStringList labels;
	int selectedIndex = 0;
	// Where the selection is right now, in index units. Equal to selectedIndex
	// at rest, somewhere between two indices while it travels. A skin that
	// slides an indicator reads this; a skin that simply fills the chosen cell
	// reads selectedIndex and ignores it.
	double selectionPosition = 0.0;
	int hoveredIndex = -1;
	int pressedIndex = -1;
	bool focused = false;
	bool enabled = true;

	// The cell a given index occupies. Fractional widths are kept rather than
	// rounded, so the cells tile the control exactly instead of leaving a seam
	// that drifts with the width.
	QRectF segmentRect(double index) const;
};

// Snapshot of one VSTBusStrip format selector (the input or output half of a
// VST3 main-bus contract) handed to ISkin::paintVstBusSelector. The widget
// owns every bit of input handling (menu popup, keyboard, focus); the skin
// only paints.
struct VstBusSelectorState
{
	QRect rect;
	// False for the input selector, true for the output selector.
	bool output = false;
	// The role in two registers: the untranslated engraving token ("IN" /
	// "OUT") for the skins whose constitutions print hardware/board/terminal
	// markings, and the translated caption for the skins that speak the
	// user's language. Each skin picks one; both describe the same selector.
	QString roleToken;
	QString roleText;
	// The config grammar's layout token ("Auto", "Stereo", "7.1.4") - data,
	// never translated.
	QString layoutText;
	// The layout's fixed width; 0 while the layout is Auto.
	int channelCount = 0;
	bool enabled = true;
	bool hovered = false;
	bool pressed = false;
	bool focused = false;
	// The layout menu is currently open on this selector.
	bool menuOpen = false;
};

// Snapshot of the VSTBusStrip around its two selectors, handed to
// ISkin::paintVstBusFrame: the optional ground panel, the joint between the
// input and output selectors (the signal direction), and the compact
// negotiation verdict readout. Painted before the selector children paint
// themselves.
struct VstBusFrameState
{
	QRect rect;
	QRect inputRect;
	QRect outputRect;
	// The gap between the selectors; where the direction mark lives.
	QRect jointRect;
	// The trailing readout area after the output selector.
	QRect verdictRect;
	// Compact verdict. Either a short word ("Rejected", "VST2") in
	// verdictText, or the negotiated pair in verdictInputText/-OutputText
	// (config grammar). The pair arrives split so each skin joins it with
	// its own direction mark - a baked-in arrow glyph would hand the joint
	// to font coverage instead of the constitution. All three empty hides
	// the readout.
	QString verdictText;
	QString verdictInputText;
	QString verdictOutputText;
	enum class Tone
	{
		Neutral,
		Success,
		Warning,
		Critical
	};
	Tone tone = Tone::Neutral;
	bool enabled = true;
};

// One slot cell of the VST channel-fill rails (VSTSlotFillRail): the
// interactive cell assigning a config channel to one negotiated bus slot.
// Distinct from VstBusSelectorState on purpose - the bus selectors pick a
// LAYOUT, these cells pick a CHANNEL, and the two must not read as the same
// control (see the fill rail section in docs/skin-hooks.md).
struct VstSlotFillCellState
{
	QRect rect;
	// False on the input rail (channel feeds the slot), true on the output
	// rail (slot writes the channel).
	bool output = false;
	// The slot's role in the negotiated layout, untranslated config grammar
	// ("L", "LFE", "RL", ...).
	QString roleToken;
	// The assigned channel ("SL", "7") or "-" while silent/discarded.
	QString valueText;
	// The "-" slot: silence into the plugin, or plugin output discarded.
	bool silent = false;
	// No explicit list on this side yet: the shown value is the engine's
	// implicit default. Skins render it quieter than a committed value.
	bool defaulted = false;
	// The committed value does not resolve into the channels selected at
	// this row; the engine would disable the plugin over it.
	bool missingChannel = false;
	bool enabled = true;
	bool hovered = false;
	bool pressed = false;
	bool focused = false;
	bool menuOpen = false;
};

// The rail around the slot cells, painted before the cell children. The
// latch (the whole feature's fold affordance) belongs to the rail frame, NOT
// to the cell grammar: it must not look like one more channel dropdown.
struct VstSlotFillRailState
{
	QRect rect;
	bool output = false;
	// The cells' bounding area (empty while collapsed).
	QRect cellsRect;
	// The fold latch area on the input rail; null when the latch is absent
	// (single-rail cards do not fold).
	QRect latchRect;
	bool collapsed = false;
	bool latchHovered = false;
	bool latchPressed = false;
	bool latchFocused = false;
	bool enabled = true;
};

// Snapshot of an AudioKnob's state handed to ISkin::paintKnob. The widget owns
// all input handling; the skin only paints.
struct KnobState
{
	int value = 0;
	int minimum = 0;
	int maximum = 0;
	// value mapped onto 0..1 (0 when the range is empty).
	double ratio = 0.0;
	// Gain-style knob: neutral at the range centre (12 o'clock). Skins should
	// render bipolar and unipolar knobs distinguishably.
	bool bipolar = false;
	// Centred text when non-empty (e.g. "3.2 dB"). Empty for promoted legacy
	// dials, which show their value in a separate spin box.
	QString valueText;
	bool enabled = true;
	bool hovered = false;
	bool dragging = false;
	bool focused = false;
};

// How AudioKnob turns pointer motion into value changes. The widget owns
// both gestures (Editor/widgets/AudioKnob.cpp); a skin only names the one
// its instrument is built for, and paints the instrument to match: a
// rotary knob follows the pointer's angle about its centre, a rolling drum
// follows the pointer's vertical travel (KnobTravel::RangePixels for the
// whole range, Editor/widgets/KnobTravel.h; a painter that derives its
// surface travel from the same figure rolls under the pointer one to one).
enum class KnobGesture
{
	Rotary,
	VerticalDrag
};

class ISkin
{
public:
	virtual ~ISkin() = default;

	// Stable identifier persisted in settings (e.g. "studio", "matrix").
	virtual QString id() const = 0;

	// Colour + metric tokens for the requested mode. The default resolves the
	// table SkinThemeData keeps for id(); the five shipped skins live there,
	// so they do not override this.
	virtual SkinTokens tokens(bool dark) const;

	// Resource path of the QSS sheet for the requested mode. Default:
	// SkinThemeData::qssResource(id(), dark), which also carries the minimal
	// skin's historical precision_* file names.
	virtual QString qssResource(bool dark) const;

	// The Copy routing renderer that matches this skin's philosophy. May be
	// nullptr, in which case the caller falls back to the legacy CopyFilterGUI.
	virtual IRoutingRenderer* routingRenderer() const = 0;

	// Paint a knob into rect. AudioKnob keeps all input handling (rotary drag,
	// wheel, keyboard) and delegates only the painting here. The default
	// implementation (ISkin.cpp) is the shared arc-knob rendering; it
	// deliberately ignores the hover/drag/focus state flags. Skins override
	// this to give knobs their own philosophy.
	virtual void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const;

	// The pointer gesture AudioKnob uses for this skin's knobs. Default:
	// Rotary, the gesture of every arc knob. A skin answers VerticalDrag when
	// its knob is a surface that rolls (minimal's register drum); the widget
	// then also rests a vertical-arrow cursor on it.
	virtual KnobGesture knobGesture() const;

	// Inline stylesheet for the modern card's frame (QFrame#FilterCardRow),
	// re-evaluated whenever the row's state changes. The default reproduces
	// the shared token-driven chrome (uniform 1px border, plus the accent rail
	// when tokens.cardRailWidth > 0). Skins override to give command types
	// their own frame treatment.
	virtual QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const;

	// Inline stylesheet for the card's header strip (QWidget#FilterCardHeader).
	virtual QString cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const;

	// Inline stylesheet for the header's type badge (QLabel#FilterTypeBadge).
	// typeColor is the per-command-type colour from FilterCardModel. The
	// default is the shared OutlineOnly/filled treatment; skins override it
	// when their constitution reserves colour for other semantics (e.g.
	// matrix keeps traffic-light colours for status only and renders a
	// monochrome type cell).
	// badgeToken is the descriptor's monogram (the biquad type code for
	// Filter rows), which Studio folds onto its band families.
	virtual BadgeTreatment badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
		const QString& badgeToken, const SkinTokens& tokens) const;

	// Called once when a command row or a command body editor is built, so a
	// skin can tag widgets with dynamic properties or attach extra chrome.
	// For modern card rows card/header/body are the frame, header strip and
	// body stack; for body editors that consult the hook themselves (the
	// Include/VST card editors and the legacy Include/VST rows) only body is
	// set. Default: no-op.
	//
	// tokens is passed like every other hook's. It was the one hook without it,
	// and all five skins reached for SkinManager::instance()->tokens() instead -
	// which works only because the singleton happens to hold the skin being asked.
	virtual void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
		const SkinTokens& tokens) const;

	// Painted decoration over the card frame's QSS background (rails, screws,
	// per-type markers). Runs after the frame's stylesheet background and
	// before child widgets paint. Default: no-op.
	virtual void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const;

	// The row's left scope gutter: the indent margin left of the card frame,
	// which carries the channel-group rail and the If-block scope lane.
	// Return true to replace the shared default
	// (FilterCardRow's channelGroupStyle rail) for this row; the neutral
	// default paints nothing and returns false, so every skin stays
	// pixel-identical until it answers. size is the row widget's full size, and
	// info carries the lane geometry (cardLeft, laneCount, laneCenter).
	virtual bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const;

	// One channel-scope token in a card header (the "Channel:'s influence"
	// badges). Return true to replace ChBadge's shared chip painting; the
	// neutral default keeps it, so every skin stays pixel-identical until
	// it answers. virtualChannel marks a Copy-created (unverified) name.
	virtual bool paintChannelBadge(QPainter& painter, const QRect& rect, const QString& channel,
		bool virtualChannel, const SkinTokens& tokens) const;

	// Layout policy for the If family's branch/tail rows (ElseIf/Else/EndIf):
	// true indents them with the block members (logicDepth) instead of at
	// their head's level, so a painted scope lane in the gutter passes them
	// visibly instead of dying behind their full-width faces. Default:
	// false - semantic indentation, branch rows align with their If head.
	virtual bool logicSiblingsIndentAsMembers() const;

	// The persistent "add card" row at the end of the filter list (shared
	// insertion contract, docs/skins/README.md). The AddCardRow widget owns
	// input (click / Enter opens the filter picker anchored under the row) and
	// delegates all painting here. The default is a neutral token-driven ghost
	// row: dashed border, muted "+ <label>" caption, accent on hover. Skins
	// override to answer with their own philosophy.
	virtual void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const;

	// The hover-only insertion seam above the FIRST card: a direct alternative
	// to that card header's "+" (both insert at the leading edge). The widget
	// is invisible at rest and only paints while hovered, so this hook never
	// changes a skin's at-rest gallery. The default is a thin accent line with
	// a small "+" disc at the left edge.
	virtual void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const;

	// The GraphicEQ card's response plot (GraphicEQPlotWidget) - the clean
	// install's first impression. The widget owns the model and all input;
	// the skin paints everything: ground, grid, labels, the response curve,
	// the node handles and the cursor readout. The default is a neutral
	// token-driven rendering; each shipped skin answers with its own
	// instrument (form decided in paint code, not QSS).
	virtual void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const;

	// The analysis dock's response graph (EqGraphView): the whole config's
	// measured response. The widget owns sampling, axis fitting and cursor
	// tracking; the skin paints everything - ground, grid, labels, the
	// response trace, the clip emphasis above 0 dB and the cursor readout.
	// The default is a neutral token-driven rendering (also the heritage
	// look); each shipped skin answers with the same instrument family it
	// chose for the GraphicEQ plot.
	virtual void paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const;

	// A row of mutually exclusive choices, all visible at once: the analysis
	// graph's metric, an all-pass section's order. The widget owns every bit of
	// input handling and hands over a finished state; the skin only draws.
	//
	// One control for both uses on purpose. Two visual conventions for "pick
	// one of these few" would be a second grammar to learn and a second thing
	// to keep consistent across five skins.
	//
	// The default (ISkin.cpp) is a token-coloured pill with the chosen cell
	// filled. A skin that slides an indicator should read
	// state.selectionPosition rather than state.selectedIndex, so a quick run
	// through three choices reads as one travelling mark instead of three
	// jumps.
	virtual void paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const;

	// One format selector of the VST bus strip (VSTBusStrip): the interactive
	// cell that names the input or output layout of a VST3 main-bus contract.
	// The widget owns the popup menu, keyboard and focus handling and hands
	// over a finished state; the skin only draws. The default (ISkin.cpp) is
	// a neutral token-driven strip: muted role caption, the layout token in
	// data ink, a channel count and a caret. Skins override to answer with
	// their own instrument (glass strip, terminal caption, pill chip, latch
	// cap, board cell).
	virtual void paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const;

	// The strip around the two selectors: ground panel (if the skin mounts
	// one), the direction mark in the joint, and the compact negotiation
	// verdict readout. Runs before the selector children paint. The default
	// is a painted arrow in muted ink plus the verdict text behind a small
	// tone lamp.
	virtual void paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const;

	// One slot cell of the VST channel-fill rails: role engraving, the
	// assigned channel, and the dropdown cue, sized by the widget so text
	// never collides with the caret. The default is a neutral cell that
	// reads visibly different from paintVstBusSelector (no channel-count
	// suffix, silent slots in muted strike ink, missing channels in the
	// danger tone).
	virtual void paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const;

	// The rail under a row of slot cells plus the fold latch. The latch is
	// the feature's on/off fold and must be a different affordance than the
	// channel cells (the default draws a small labelled toggle, not another
	// dropdown).
	virtual void paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const;

	// The "add filter" picker that matches this skin's philosophy. The caller
	// (FilterTable::chooseFilterTemplate) hosts the returned view in a
	// dropdown-style Qt::Popup container anchored at the add button, feeds it
	// the template entries and waits for entryChosen()/dismissed(). The
	// default (ISkin.cpp) is the neutral DefaultFilterPickerView: a search
	// field over one sectioned list. Ownership passes to the caller via the
	// usual QWidget parent mechanism.
	//
	// tokens is passed like every other hook's (audit #275 B1, the S3
	// treatment): the views these hooks build used to reach for
	// SkinManager::instance() in their constructors and paintEvents, which
	// works only while the singleton happens to hold the skin being asked.
	virtual FilterPickerView* createFilterPicker(QWidget* parent, const SkinTokens& tokens) const;

	// The reference-card body view for rows that point at an external file
	// (kind: "include", "convolution", "multiconvolution", "vst"). The host
	// editor owns all behavior and drives the view through
	// ReferenceCardView::setState; the view owns structure and presentation,
	// so each skin can answer the same reference with its own grammar instead
	// of a palette swap. The default (ISkin.cpp) is the neutral
	// DefaultReferenceCardView. Ownership passes to the caller via the usual
	// QWidget parent mechanism.
	virtual ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent,
		const SkinTokens& tokens) const;

	// The compact SubwooferRouting card body. The editor computes and owns the
	// state and actions; the returned view owns their presentation. The
	// default is DefaultSubwooferRoutingCardView.
	virtual SubwooferRoutingCardView* createSubwooferRoutingCardView(QWidget* parent,
		const SkinTokens& tokens) const;

	// Painted decoration over the custom title bar's QSS background (screws,
	// grid texture, glows - whatever the skin's constitution calls for).
	// Drawn by TitleBar::paintEvent after the stylesheet background and
	// before child widgets. Default: no-op.
	virtual void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const;

	// Dress the main toolbar in this skin's language. Called from
	// applyRedesignPreferences at startup and again on every skin/dark
	// switch, so implementations must be idempotent. The default replaces the
	// legacy .ico action icons (the 2005-era document set) with the shared
	// modern stroke icons tinted by the text token and sets a scaled icon
	// size; skins override to re-tint, swap the icon language, set dynamic
	// properties their QSS targets, or attach painted chrome. The file
	// actions are identified by their .ui object names (actionNew,
	// actionOpen, actionSave).
	virtual void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const;

	// Dress a non-native QFileDialog (GUIHelper::prepareFileDialog) in this
	// skin's language. The dialog's widget tree is assembled from primitives
	// the skin sheet already styles (tree/list views, header sections,
	// combos, buttons, scrollbars); this hook answers the parts QSS cannot
	// reach - the navigation tool buttons' icons, which QFileDialog seeds
	// from the platform style. Buttons are identified by Qt's internal
	// object names (backButton, forwardButton, toParentButton,
	// newFolderButton, listModeButton, detailModeButton). The default is the
	// shared modern stroke set tinted with the text token; skins override to
	// re-tint or swap the icon language, mirroring their styleMainToolbar
	// answer. Never called in heritage mode (the dialog stays native there).
	virtual void styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const;
};
