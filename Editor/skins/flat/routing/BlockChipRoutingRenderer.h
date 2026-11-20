/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The Stock Flat skin's Copy renderer (docs/skins/flat.md, Copy routing):
	each output is one line of an equation, a target label, an equals sign and
	one bordered cell per source holding its factor and channel. The list
	folds: only the channels the command involves get a line, the rest of the
	device layout waits behind "show more", and "add channel" names a new
	virtual channel.
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
