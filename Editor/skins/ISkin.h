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

#include <QRect>
#include <QString>

#include "Editor/SkinTokens.h"

class FilterPickerView;
class IRoutingRenderer;
class QPainter;
class QWidget;

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

	// Colour + metric tokens for the requested mode.
	virtual SkinTokens tokens(bool dark) const = 0;

	// Resource path of the QSS sheet for the requested mode.
	virtual QString qssResource(bool dark) const = 0;

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

	// The "add filter" picker that matches this skin's philosophy. The caller
	// (FilterTable::chooseFilterTemplate) hosts the returned view in a
	// dropdown-style Qt::Popup container anchored at the add button, feeds it
	// the template entries and waits for entryChosen()/dismissed(). The
	// default (ISkin.cpp) is the neutral DefaultFilterPickerView: a search
	// field over one sectioned list. Ownership passes to the caller via the
	// usual QWidget parent mechanism.
	virtual FilterPickerView* createFilterPicker(QWidget* parent) const;
};
