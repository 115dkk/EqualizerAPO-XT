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
	Device/Channel switch caps. The panel folds: only the channels the
	command involves are mounted by default, the rest of the device layout
	sits behind the +N expansion latch, and the ADD button patches a new
	virtual channel label onto the faceplate.
*/

#pragma once

#include <QLineEdit>

#include "Editor/widgets/routing/CopyRoutingAdapter.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/RoutingFold.h"

class HardwarePatchbayView : public RoutingView
{
	Q_OBJECT

public:
	HardwarePatchbayView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent);

	std::vector<Assignment> assignments() const override;
	void galleryShowcase(const QString& state) override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void rebuildMatrix();
	void updateMetrics();
	Assignment& rowAssignment(int outRow);
	int summandIndex(int outRow, const QString& channel) const;
	QRect cellRect(int outRow, int inCol) const;
	bool hitTest(const QPoint& pos, int& outRow, int& inCol) const;
	QRect stripRect() const;
	void commitEditor();
	void openChannelEditor();
	void commitChannelEditor();

	std::vector<Assignment> workingAssignments;
	// Device channel layout; keeps the full patch-bay reachable even when the
	// command references few (or no) channels.
	std::vector<std::wstring> deviceChannels;
	// Fixed-source mode (MultiConvolution): input columns come only from
	// portModel.fixedSources and factors are locked to unity; target rows use
	// the same fold as Copy.
	RoutingPortModel portModel;
	CopyRoutingAdapter::Matrix matrix;

	// Channel fold: rowMap translates a panel row back to its seeded
	// assignment; pinned channels stay mounted while their sum is empty.
	bool channelsExpanded = false;
	QStringList pinnedChannels;
	RoutingFold::Fold fold;
	QVector<int> rowMap;

	// Faceplate strip controls (expansion latch + ADD button) and the per-row
	// unpatch target for virtual channel labels.
	QRect revealRect;
	QRect addRect;
	QVector<QRect> removeRects;
	int hoveredControl = 0; // 0 none, 1 reveal, 2 add
	int hoveredRow = -1;

	QLineEdit* editor = nullptr;
	int editRow = -1;
	int editCol = -1;
	QLineEdit* channelEditor = nullptr;

	// Sized from the engraved labels in updateMetrics(); the defaults fit a
	// Copy row's short device names.
	int rowHeaderWidth = 60;
	int colHeaderHeight = 34;
	int cellW = 56;
	int cellH = 50;
	int stripH = 34;
};

class HardwarePatchbayRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent) override;
	const char* id() const override { return "hardware-patchbay"; }
};
