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

class IRoutingRenderer;
class QPainter;

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
};
