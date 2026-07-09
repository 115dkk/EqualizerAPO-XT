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

class FilterPickerView;
class IRoutingRenderer;
class QPainter;
class QToolBar;
class QWidget;
class ReferenceCardView;

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
	// (prepareCommandRow) always see false. Skins that ignore it keep their
	// exact pre-hover appearance.
	bool hovered = false;
	int depth = 0;
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
	// implementation (ISkin.cpp) reproduces the shared arc-knob rendering
	// pixel-identically; it deliberately ignores the hover/drag/focus state
	// flags so that adding the hook changed nothing visually. Skins override
	// this to give knobs their own philosophy.
	virtual void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const;

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
	// default reproduces the shared OutlineOnly/filled treatment every skin
	// used before the hook existed; skins override it when their constitution
	// reserves colour for other semantics (e.g. matrix keeps traffic-light
	// colours for status only and renders a monochrome type cell).
	virtual QString typeBadgeStyle(const CommandRowInfo& info, const QString& typeColor, const SkinTokens& tokens) const;

	// Ink for the pictogram the card header places inside the type badge
	// (feedback round 2: pictures replace the English monograms, matching the
	// picker tiles). A tinted pixmap cannot follow the QSS 'color' rule the
	// badge style writes, so each skin restates its badge ink here and the
	// two must stay in step. badgeToken is the descriptor's monogram (the
	// biquad type code for Filter rows), which studio folds onto its band
	// families. The default mirrors the default typeBadgeStyle ink.
	virtual QColor typeBadgeInk(const CommandRowInfo& info, const QString& typeColor, const QString& badgeToken, const SkinTokens& tokens) const;

	// Called once when a command row or a command body editor is built, so a
	// skin can tag widgets with dynamic properties or attach extra chrome.
	// For modern card rows card/header/body are the frame, header strip and
	// body stack; for body editors that consult the hook themselves (the
	// Include/VST card editors and the legacy Include/VST rows) only body is
	// set. Default: no-op.
	virtual void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body) const;

	// Painted decoration over the card frame's QSS background (rails, screws,
	// per-type markers). Runs after the frame's stylesheet background and
	// before child widgets paint. Default: no-op.
	virtual void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const;

	// The persistent "add card" row at the end of the filter list (shared
	// insertion contract, docs/skins/README.md). The AddCardRow widget owns
	// input (click / Enter opens the filter picker anchored under the row) and
	// delegates all painting here. The default is a neutral token-driven ghost
	// row: dashed border, muted "+ <label>" caption, accent on hover. Skins
	// override to answer with their own philosophy.
	virtual void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const;

	// The hover-only insertion seam above the FIRST card (the one place a new
	// card can be inserted in front of everything, since the header "+" adds
	// below its card). The widget is invisible at rest and only paints while
	// hovered, so this hook never changes a skin's at-rest gallery. The default
	// is a thin accent line with a small "+" disc at the left edge.
	virtual void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const;

	// The GraphicEQ card's response plot (GraphicEQPlotWidget) - the clean
	// install's first impression. The widget owns the model and all input;
	// the skin paints everything: ground, grid, labels, the response curve,
	// the node handles and the cursor readout. The default is a neutral
	// token-driven rendering; each shipped skin answers with its own
	// instrument (form decided in paint code, not QSS).
	virtual void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const;

	// The "add filter" picker that matches this skin's philosophy. The caller
	// (FilterTable::chooseFilterTemplate) hosts the returned view in a
	// dropdown-style Qt::Popup container anchored at the add button, feeds it
	// the template entries and waits for entryChosen()/dismissed(). The
	// default (ISkin.cpp) is the neutral DefaultFilterPickerView: a search
	// field over one sectioned list. Ownership passes to the caller via the
	// usual QWidget parent mechanism.
	virtual FilterPickerView* createFilterPicker(QWidget* parent) const;

	// The reference-card body view for rows that point at an external file
	// (kind: "include", "convolution", "multiconvolution", "vst"). The host
	// editor owns all behavior and drives the view through
	// ReferenceCardView::setState; the view owns structure and presentation,
	// so each skin can answer the same reference with its own grammar instead
	// of a palette swap. The default (ISkin.cpp) is the neutral
	// DefaultReferenceCardView. Ownership passes to the caller via the usual
	// QWidget parent mechanism.
	virtual ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const;

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
};
