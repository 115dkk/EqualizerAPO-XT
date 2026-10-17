/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Soft skin's Copy renderer: each output is a soft, rounded "equation block"
	reading like VSL = 0.86·L − 0.5·R, built from friendly channel chips and
	factor chips. Tactile and approachable, matching the Soft skin philosophy.
	The block list folds: only the channels the command involves get a block,
	the rest of the device layout waits behind a quiet "show more" pill, and a
	dashed "add channel" chip (the not-hardware-backed grammar) lets a new
	virtual channel be named.
*/

#pragma once

#include "Editor/widgets/routing/SumRoutingViewBase.h"

class BlockChipView : public SumRoutingViewBase
{
	Q_OBJECT

public:
	BlockChipView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	int slotAt(const QPoint& pos) const override;
	QRect summandEditorRect(const QRect& span, int minimumWidth) const override;
	QRect channelEditorRect() const override;

private:
	int blockH = 46;
	int gap = 8;
	int controlH = 30;
};

class BlockChipRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens) override;
	const char* id() const override { return "block-chip"; }
};
