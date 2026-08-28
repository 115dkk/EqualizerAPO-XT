/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Hardware Rack's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies): the unit's service face - status lamp, engraved
	label strip, recessed LCD readout. Rack's hardware grammar is expressed by
	helpers local to this file pair.
	Constitution: docs/skins/rack.md ("참조 카드" section).
*/

#pragma once

#include <QColor>
#include <QFont>

#include "Editor/widgets/cards/ReferenceCardView.h"
#include "Editor/SkinTokens.h"

class QHBoxLayout;

// Engraved faceplate printing (flat in the depth hierarchy): a contrast pass
// offset one pixel down - the recess edge catching the light - then the ink,
// Rack's engraved-text formula as a widget. Elides at paint time so the
// visible portion stays correct across resizes; optionally framed as a
// stamped wireframe tag, or warmed amber under the cursor (the lamp grammar
// for the clickable identity - never a button lift).
class RackEngravedLabel : public QWidget
{
	// Q_OBJECT so the class name reaches QSS: the rack sheets select these
	// faceplate widgets by class to keep the global QWidget base coat from
	// patching their rects.
	Q_OBJECT

public:
	enum class Ink
	{
		Body,
		Muted,
		Warning,
		Danger
	};

	explicit RackEngravedLabel(const SkinTokens& tokens, QWidget* parent = nullptr);

	void setText(const QString& newText);
	void setInk(Ink newInk);
	// The missing reference stays on the plate but recedes.
	void setDimmed(bool newDimmed);
	// Amber warmth under the cursor while the identity is clickable.
	void setHotTrack(bool newHotTrack);
	void setPixelSize(int newPixelSize);
	void setLetterSpacing(qreal newLetterSpacing);
	void setBoldFace(bool newBoldFace);
	// Printed wireframe outline around the text (the badge law: no fills).
	void setStamped(bool newStamped);
	void setElideMode(Qt::TextElideMode newElideMode);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	const SkinTokens skinTokens;
	QFont engraveFont() const;

	QString text;
	Ink ink = Ink::Body;
	bool dimmed = false;
	bool hotTrack = false;
	int pixelSize = 12;
	qreal letterSpacing = 0.0;
	bool boldFace = true;
	bool stamped = false;
	Qt::TextElideMode elideMode = Qt::ElideNone;
};

// A bezel-set panel LED (Rack's panel-lamp grammar: bezel ring, halo
// while lit, gradient dome, specular dot). The reference's health is read
// from this lamp: green = resolved, amber = service warning, red = broken
// reference / unreadable target, dark = empty slot. A powered-down unit
// (disabled row) always shows the dome gone dark.
class RackStatusLamp : public QWidget
{
	Q_OBJECT

public:
	explicit RackStatusLamp(const SkinTokens& tokens, QWidget* parent = nullptr);

	void setLamp(const QColor& color, bool newLit);

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	const SkinTokens skinTokens;
	QColor litColor;
	bool lit = false;
};

// A recessed LCD readout window set into the plate for the measured facts of
// an impulse response. The display-window clause applies: the glass stays
// dark in BOTH modes and the segments burn green - the same glass and inks
// as the knob's LED window. Segments elide at paint
// time; the full readout lives in the tooltip.
class RackLcdWindow : public QWidget
{
	Q_OBJECT

public:
	explicit RackLcdWindow(const SkinTokens& tokens, QWidget* parent = nullptr);

	void setSegments(const QString& newText);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	const SkinTokens skinTokens;
	QFont segmentFont() const;
	QString displayText() const;

	QString text;
};

class RackReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit RackReferenceCardView(const QString& kind, const SkinTokens& tokens, QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;
	void placeBusStrip(QWidget* strip) override;

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
	void applyState(const ReferenceCardState& state) override;

private:
	const SkinTokens skinTokens;
	QHBoxLayout* rootLayout = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	RackStatusLamp* lamp = nullptr;
	RackEngravedLabel* captionLabel = nullptr;
	RackEngravedLabel* absStamp = nullptr;
	RackEngravedLabel* serviceTag = nullptr;
	RackEngravedLabel* nameLabel = nullptr;
	RackEngravedLabel* dirLabel = nullptr;
	RackEngravedLabel* statusLabel = nullptr;
	RackLcdWindow* lcdWindow = nullptr;
};
