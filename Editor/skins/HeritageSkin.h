/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The heritage presentation as a skin (audit #275 B5). Heritage mode - the
	frozen legacy-rows look - used to live as six special-case branches inside
	SkinManager's forwarders, three of which literally called the ISkin base
	implementation. That is the definition of a base-class skin: classic light
	tokens, no QSS, no routing renderer, native toolbar and file dialog, and
	the neutral base painters for everything else. This adapter says so in one
	place, so the forwarders forward uniformly and adding a hook no longer
	means asking "and what does heritage do?" inside SkinManager.

	Since audit #422 S3 the hooks every skin must answer are pure virtual in
	ISkin, and their neutral rendering lives here rather than in the base;
	the defaults that product skins still reach (the knob, the analysis
	graph, the segmented control) stay in ISkin.

	Deliberately not in SkinThemeData's roster: heritage is not a selectable
	skin program, it is what the legacy-rows preference renders as. It has no
	constitution under docs/skins/ for the same reason.
*/

#pragma once

#include "ISkin.h"

class HeritageSkin : public ISkin
{
public:
	QString id() const override;
	// Classic light values for the custom painters that consume tokens; the
	// dark flag is ignored (heritage is the classic light look, always).
	SkinTokens tokens(bool dark) const override;
	// No skin routing view: Copy rows keep the legacy CopyFilterGUI.
	IRoutingRenderer* routingRenderer() const override;
	// Native toolbar and platform file dialog: the skin adds nothing.
	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override;
	void styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const override;
	// No provider: the native dialog keeps Qt's own pictograms.
	QAbstractFileIconProvider* createFileIconProvider(const SkinTokens& tokens) const override;
	// Native rows draw no scope lanes: branch rows align with their If head.
	bool logicSiblingsIndentAsMembers() const override;

	// The neutral token-driven answers to the hooks every skin must give.
	// Heritage never constructs the modern cards, the list chrome, the
	// GraphicEQ plot or the VST card that call them.
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override;
	QString cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override;
	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override;
	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override;
	void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const override;
	void paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const override;
	void paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const override;
	void paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const override;
	void paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const override;
};

// The one instance, like the per-skin accessors in Skins.cpp.
ISkin* heritageSkin();
