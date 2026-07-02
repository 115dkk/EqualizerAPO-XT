/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Hardware Rack's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies). See docs/skins/rack.md for the constitution this
	answers to.

	The reference is presented as the unit's service face, painted straight on
	the brushed plate: a bezel-set status lamp (state = lamps), the target's
	name as engraved faceplate printing under a letter-spaced function caption
	with stamped wireframe tags (VST2/VST3, ABS), the location as small
	engraved sub-printing, and the impulse-response readout as a recessed LCD
	window whose glass stays dark in both modes (the display-window clause).
	A broken reference is a service condition, not an alarm wall: the lamp
	turns red, the name engraving recedes, a small engraved NOT FOUND caption
	appears and the Browse cap becomes the LOCATE service button. All painting
	imitates RackChrome's grammar (engraved double-pass printing, bezel LED
	domes, the knob window's dark glass and segment inks) as helpers local to
	this file pair, so the body stays visually continuous with the faceplate
	chrome without touching the shared RackChrome.
*/

#pragma once

#include <QColor>
#include <QFont>

#include "Editor/widgets/cards/ReferenceCardView.h"

class QHBoxLayout;

// Engraved faceplate printing (flat in the depth hierarchy): a contrast pass
// offset one pixel down - the recess edge catching the light - then the ink,
// RackChrome::engraveText's formula as a widget. Elides at paint time so the
// visible portion stays correct across resizes; optionally framed as a
// stamped wireframe tag, or warmed amber under the cursor (the lamp grammar
// for the clickable identity - never a button lift).
class RackEngravedLabel : public QWidget
{
public:
	enum class Ink
	{
		Body,
		Muted,
		Warning,
		Danger
	};

	explicit RackEngravedLabel(QWidget* parent = nullptr);

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

// A bezel-set panel LED (RackChrome::paintLed's grammar: bezel ring, halo
// while lit, gradient dome, specular dot). The reference's health is read
// from this lamp: green = resolved, amber = service warning, red = broken
// reference / unreadable target, dark = empty slot. A powered-down unit
// (disabled row) always shows the dome gone dark.
class RackStatusLamp : public QWidget
{
public:
	explicit RackStatusLamp(QWidget* parent = nullptr);

	void setLamp(const QColor& color, bool newLit);

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QColor litColor;
	bool lit = false;
};

// A recessed LCD readout window set into the plate for the measured facts of
// an impulse response. The display-window clause applies: the glass stays
// dark in BOTH modes and the segments burn green - the same glass and inks
// as the knob's LED window in RackChrome::paintKnob. Segments elide at paint
// time; the full readout lives in the tooltip.
class RackLcdWindow : public QWidget
{
public:
	explicit RackLcdWindow(QWidget* parent = nullptr);

	void setSegments(const QString& newText);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QFont segmentFont() const;
	QString displayText() const;

	QString text;
};

class RackReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit RackReferenceCardView(const QString& kind, QWidget* parent = nullptr);

	void addActionButton(ActionRole role, QAbstractButton* button) override;
	void addLeadingWidget(QWidget* widget) override;

protected:
	void applyState(const ReferenceCardState& state) override;

private:
	QHBoxLayout* rootLayout = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	RackStatusLamp* lamp = nullptr;
	RackEngravedLabel* captionLabel = nullptr;
	RackEngravedLabel* formatStamp = nullptr;
	RackEngravedLabel* absStamp = nullptr;
	RackEngravedLabel* serviceTag = nullptr;
	RackEngravedLabel* nameLabel = nullptr;
	RackEngravedLabel* dirLabel = nullptr;
	RackEngravedLabel* statusLabel = nullptr;
	RackLcdWindow* lcdWindow = nullptr;
	QAbstractButton* browseButton = nullptr;
};
