/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Signal Matrix skin's Copy renderer: a flat crosspoint grid (input columns ×
	output rows) where each cell encodes the routing coefficient by colour and
	number, in the manner of an audio routing matrix / patch-bay. Best for the
	"is input X routed to output Y, and at what gain" lookup in dense
	multi-channel (7.1 + virtual) configurations. The grid folds: a collapsed
	view lays out only the channels the command involves, the rest of the
	device layout waits behind the +N CH caption cell, and +BUS patches a new
	virtual channel into the board.
*/

#pragma once

#include "Editor/widgets/routing/GridRoutingViewBase.h"

class CrosspointMatrixView : public GridRoutingViewBase
{
	Q_OBJECT

public:
	CrosspointMatrixView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void updateMetrics() override;
	QRect factorEditorRect(int outRow, int inCol) const override;
	QRect channelEditorRect() const override;

private:
	// The footer strip of caption cells (the demoted-caption grammar of the
	// Device card's reveal toggle) under the grid.
	QRect footerRect() const;

	int footerH = 24;
};

class CrosspointMatrixRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens) override;
	const char* id() const override { return "crosspoint-matrix"; }
};
