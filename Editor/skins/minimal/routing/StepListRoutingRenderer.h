/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Minimal skin's Copy renderer: a monospace, terminal-like sequential step
	list. Each output is one row "# │ Dest ← Sources" with explicit + / − signs,
	×N gain factors and an INV marker for phase inversion. This is the academic,
	unambiguous text+number form. The listing folds like a pager: only the
	steps the command involves are listed, the rest of the device layout sits
	behind a fold line ("[+N CH]"), and the closing prompt is the add-channel
	entry - clicking it lets a new virtual channel be typed at the prompt.

	Editing a source (double-click on its token or gain) opens one inline
	editor holding the summand as the line writes it ("0.5*L"); retyping the
	token changes channel and factor at once, a bare factor keeps the channel
	(RoutingFold::parseSourceToken). The editor completes channel names from
	the same list the per-step [+] menu offers. The editor used to be a
	gain-only field sized to the token it covered, which clipped even "L"
	under the skin's padding and silently ignored a channel or port typed
	into it (field report: "R=0, retype 1, still 0").
*/

#pragma once

#include "Editor/widgets/routing/SumRoutingViewBase.h"

class StepListView : public SumRoutingViewBase
{
	Q_OBJECT

public:
	StepListView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	int slotAt(const QPoint& pos) const override;
	QRect summandEditorRect(const QRect& span, int minimumWidth) const override;
	QRect channelEditorRect() const override;

private:
	int rowH = 30;
	int headerH = 22;
};

class StepListRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens) override;
	const char* id() const override { return "step-list"; }
};
