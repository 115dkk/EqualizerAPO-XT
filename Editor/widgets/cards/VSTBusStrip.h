/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The modern VST card's main-bus instrument: two format selectors (the
	VST3 Input/Output contract) joined by a direction mark, with a compact
	negotiation verdict readout trailing them. Mounted beside the plugin
	identity by each skin's ReferenceCardView (placeBusStrip) - the card is
	wide, so the bus lives in the row's horizontal slack, never on a stacked
	extra row.

	The strip and its selectors own all behavior - the layout popup menu,
	keyboard access, focus, accessibility, enable/disable reasons - and hand
	painting to the active skin (ISkin::paintVstBusSelector /
	ISkin::paintVstBusFrame), the same split AudioKnob and SegmentedControl
	use. The host (VSTCardEditor) owns the semantics: what the values are,
	when they may change, and what the verdict says.
*/

#pragma once

#include <QWidget>

#include "Editor/skins/ISkin.h"
#include "vst/VST3BusLayout.h"

// One half of the contract: the interactive cell naming the input or output
// layout. Click / Space / Enter / Down opens the layout menu.
class VSTBusSelector : public QWidget
{
	Q_OBJECT

public:
	explicit VSTBusSelector(bool output, QWidget* parent = nullptr);

	VST3BusLayout busLayout() const;
	void setBusLayout(VST3BusLayout layout);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	// The user picked a layout from the menu (it may equal the current one).
	void busLayoutPicked(VST3BusLayout layout);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;

private:
	void openMenu();
	void refreshAccessibleValue();

	bool output = false;
	VST3BusLayout current = VST3BusLayout::Auto;
	bool hovered = false;
	bool pressed = false;
	bool menuOpen = false;
};

class VSTBusStrip : public QWidget
{
	Q_OBJECT

public:
	explicit VSTBusStrip(QWidget* parent = nullptr);

	VST3BusLayout inputLayout() const;
	VST3BusLayout outputLayout() const;
	// Host-driven value display; never emits busLayoutsPicked.
	void setBusLayouts(VST3BusLayout input, VST3BusLayout output);

	// Disables/enables both selectors; the reason lands in their tooltips
	// (empty restores the default tooltip), so a pointer over a locked
	// selector always learns why it is locked.
	void setSelectorsEnabled(bool enabled, const QString& disabledReason = QString());

	// The compact verdict readout after the output selector. Long-form
	// messages are not the strip's register - the host routes those through
	// the reference card's status line. setVerdict carries a short word;
	// setVerdictPair carries the negotiated layouts, split so the skin joins
	// them with its own direction mark.
	void setVerdict(const QString& text, VstBusFrameState::Tone tone);
	void setVerdictPair(const QString& input, const QString& output, VstBusFrameState::Tone tone);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	// A selector committed a value; carries the whole pair so the host deals
	// in complete contracts only.
	void busLayoutsPicked(VST3BusLayout input, VST3BusLayout output);

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	VstBusFrameState frameState() const;
	void relayout();
	int verdictWidth() const;

	VSTBusSelector* inputSelector = nullptr;
	VSTBusSelector* outputSelector = nullptr;
	QString verdictText;
	QString verdictInputText;
	QString verdictOutputText;
	VstBusFrameState::Tone verdictTone = VstBusFrameState::Tone::Neutral;
};
