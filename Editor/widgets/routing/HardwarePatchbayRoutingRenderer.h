/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Rack skin's Copy renderer: a hardware ROUTING MATRIX button field. The
	same crosspoint grid as the Signal Matrix, but each crosspoint is a small
	square illuminated latching button mounted in a recessed sub-panel - the
	control real routing matrices use for crosspoints, deliberately NOT a
	miniature of the filter cards' rotary dials. At rest a crosspoint is a
	raised blank cap; a routed crosspoint sits latched down with the amber
	lamp lit under it and the gain engraved on the cap as the button legend
	(negative gain takes the danger lamp). Same latch-down grammar as the
	Device/Channel switch caps.
*/

#pragma once

#include <QLineEdit>

#include "IRoutingRenderer.h"
#include "CopyRoutingAdapter.h"

class HardwarePatchbayView : public RoutingView
{
	Q_OBJECT

public:
	HardwarePatchbayView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent);

	std::vector<Assignment> assignments() const override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
	void rebuildMatrix();
	int summandIndex(int outRow, const QString& channel) const;
	QRect cellRect(int outRow, int inCol) const;
	bool hitTest(const QPoint& pos, int& outRow, int& inCol) const;
	void commitEditor();

	std::vector<Assignment> workingAssignments;
	// Device channel layout; keeps the full patch-bay clickable even when the
	// command references few (or no) channels.
	std::vector<std::wstring> deviceChannels;
	// Fixed-source mode (MultiConvolution): input columns come only from
	// portModel.fixedSources, and factors are locked to unity.
	RoutingPortModel portModel;
	CopyRoutingAdapter::Matrix matrix;

	QLineEdit* editor = nullptr;
	int editRow = -1;
	int editCol = -1;

	int rowHeaderWidth = 60;
	int colHeaderHeight = 34;
	int cellW = 56;
	int cellH = 50;
};

class HardwarePatchbayRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent) override;
	const char* id() const override { return "hardware-patchbay"; }
};
