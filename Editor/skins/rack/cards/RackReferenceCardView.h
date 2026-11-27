/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
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
class QSpacerItem;

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
	// Sentence printing (the status line): breaks at word boundaries over as
	// many lines as the width needs instead of eliding.
	void setWordWrap(bool newWordWrap);
	// The fewest characters an eliding label keeps on show before it lets
	// the layout squeeze it further (0 = the default 28 px floor). A text
	// no longer than this never elides.
	void setMinimumCharacters(int count);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;
	bool hasHeightForWidth() const override;
	int heightForWidth(int width) const override;

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
	bool wordWrap = false;
	int minimumCharacters = 0;
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
	// A display that may not be cut: its minimum is its whole text, so the
	// layout squeezes something else (the display clause: a readout shows
	// its whole text or is not there).
	void setWholeText(bool newWholeText);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	const SkinTokens skinTokens;
	QFont segmentFont() const;
	QString displayText() const;

	QString text;
	bool wholeText = false;
};

class RackReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit RackReferenceCardView(const QString& kind, const SkinTokens& tokens, QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;
	void placeBusStrip(QWidget* strip) override;

	// Called by RackSkin::prepareCommandRow when the unit sits in a Modern
	// row (CommandRowInfo::inlineRow). One-way: a skin or mode switch
	// rebuilds the row, so a view never goes back to the card layout. Two
	// units rearrange (identity and actions on the first line, where the
	// unit points and how it is wired or set on the second): the VST unit
	// (path and bus strip) and the Subwoofer Routing unit (path and
	// readout). Convolution and MultiConvolution keep their one line, and
	// Include keeps its face. In every row unit an LCD readout keeps its
	// whole text and the name keeps 12 characters; the path elides first.
	void setInlineRow(bool inlineRow);

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
	void applyState(const ReferenceCardState& state) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	// Row mode only: the caps go to the right end of line 1 while line 2
	// has something to show; with an empty line 2 the unit keeps the
	// card's one line, caps packed after the name.
	void updateRowArrangement();

	const SkinTokens skinTokens;
	// ReferenceCardState::kind the view was built for ("vst", "subwoofer",
	// ...): the row arrangement depends on it.
	const QString unitKind;
	bool rowMode = false;
	// updateRowArrangement is running (it reparents the path).
	bool arranging = false;
	QWidget* labelArea = nullptr;
	QWidget* busStrip = nullptr;
	// Row mode only: the second line (path, then the bus strip or the
	// readout).
	QHBoxLayout* wiringLayout = nullptr;
	// The stretch that ends line 1 (after the caps).
	QSpacerItem* trailingStretch = nullptr;
	// The unit's row (lamp, label strip, bus strip, readout, buttons); the
	// status sentence prints on its own line under it.
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
	// VST units only: the readout that leads the label strip with the
	// loaded ABI and the service condition ("VST3 MODULE", "VST MODULE
	// NOT FOUND"). Null on the other reference kinds, which keep the
	// engraved caption.
	RackLcdWindow* moduleReadout = nullptr;
};
