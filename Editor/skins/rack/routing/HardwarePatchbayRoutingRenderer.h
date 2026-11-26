/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Rack skin's Copy renderer: a hardware ROUTING MATRIX button field. The
	same crosspoint grid as the Signal Matrix, but each crosspoint is a small
	square illuminated latching button mounted in a recessed sub-panel - the
	control real routing matrices use for crosspoints, deliberately NOT a
	miniature of the filter cards' rotary dials. At rest a crosspoint is a
	raised blank cap; a routed crosspoint sits latched down with the amber
	lamp lit under it and the gain engraved on the cap as the button legend
	(negative gain takes the danger lamp). Same latch-down grammar as the
	Device/Channel switch caps. The panel folds: only the channels the
	command involves are mounted by default, the rest of the device layout
	sits behind the +N expansion latch, and the ADD button patches a new
	virtual channel label onto the faceplate.
*/

#pragma once

#include "Editor/widgets/routing/GridRoutingViewBase.h"

class HardwarePatchbayView : public GridRoutingViewBase
{
	Q_OBJECT

public:
	HardwarePatchbayView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens);

	QSize sizeHint() const override;
	QList<QPair<QRect, QString>> galleryOutlinedParts() const override { return paintedParts; }

protected:
	void paintEvent(QPaintEvent*) override;
	void updateMetrics() override;
	QRect factorEditorRect(int outRow, int inCol) const override;
	QRect channelEditorRect() const override;

private:
	// The faceplate strip under the button field (expansion latch + ADD
	// button).
	QRect stripRect() const;
	// Experiment (EAPO_MODERN_ROWS): true while the field sits in a one-line
	// unit, where every cap is a 22 px module instead of a card's 30 px cap.
	bool inInlineRow() const;
	int capHeight() const { return inlineRow ? 22 : 30; }

	bool event(QEvent* event) override;

	int stripH = 34;
	bool inlineRow = false;
	// EXPERIMENT TOOLING: the caps the last paint drew (galleryOutlinedParts).
	QList<QPair<QRect, QString>> paintedParts;
};

class HardwarePatchbayRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens) override;
	const char* id() const override { return "hardware-patchbay"; }
};
