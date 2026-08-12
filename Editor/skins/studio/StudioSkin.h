/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#pragma once

#include "Editor/skins/ISkin.h"

class StudioSkin final : public ISkin
{
public:
	QString id() const override;
	IRoutingRenderer* routingRenderer() const override;
	FilterPickerView* createFilterPicker(QWidget* parent) const override;
	ReferenceCardView* createReferenceCardView(const QString& kind, QWidget* parent) const override;
	SubwooferRoutingCardView* createSubwooferRoutingCardView(QWidget* parent) const override;

	void paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const override;
	void styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const override;
	void styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const override;
	void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const override;
	QString cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override;
	QString cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const override;
	void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens) const override;
	bool paintScopeGutter(QPainter& painter, const QSize& size, const CommandRowInfo& info, const SkinTokens& tokens) const override;
	bool logicSiblingsIndentAsMembers() const override;
	void paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override;
	void paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const override;
	void paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const override;
	void paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const override;
	void paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const override;
	BadgeTreatment badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
		const QString& badgeToken, const SkinTokens& tokens) const override;
	void prepareCommandRow(const CommandRowInfo& info, QWidget* card, QWidget* header, QWidget* body,
		const SkinTokens& tokens) const override;
};
