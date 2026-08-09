/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft skin's Copy renderer: each output is a soft, rounded "equation block"
	reading like VSL = 0.86·L − 0.5·R, built from friendly channel chips and
	factor chips. Tactile and approachable, matching the Soft skin philosophy.
	The block list folds: only the channels the command involves get a block,
	the rest of the device layout waits behind a quiet "show more" pill, and a
	dashed "add channel" chip (the not-hardware-backed grammar) lets a new
	virtual channel be named.
*/

#pragma once

#include <QLineEdit>
#include <QStringList>
#include <QVector>

#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/RoutingFold.h"
#include "filters/CopyFilter.h"

class BlockChipView : public RoutingView
{
	Q_OBJECT

public:
	BlockChipView(const std::vector<Assignment>& assignments,
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
	struct Hit { int row = 0; int summand = 0; QRect rect; };
	struct AddHit { int row = 0; QRect rect; };
	void refold();
	void commitEditor();
	void showAddMenu(int row, const QPoint& globalPos);
	void openChannelEditor();
	void commitChannelEditor();

	std::vector<Assignment> workingAssignments;
	// Device channel layout, offered by the per-block [+] chip menu.
	std::vector<std::wstring> deviceChannels;
	// Fixed-source mode (MultiConvolution): the [+] menu offers only
	// portModel.fixedSources and factors are locked to unity (a double-click
	// removes the chip instead of editing a gain). The fixed sources stay
	// available while the target blocks use the shared channel fold.
	RoutingPortModel portModel;

	// Channel fold: only fold.visibleRows get an equation block; pinned
	// channels keep their block while their sum is empty.
	bool channelsExpanded = false;
	QStringList pinnedChannels;
	RoutingFold::Fold fold;

	QVector<Hit> hits;
	QVector<AddHit> addHits;
	QVector<AddHit> removeHits; // hovered virtual dest × targets
	QRect revealRect;           // "show more / show fewer" pill
	QRect addChannelRect;       // dashed "add channel" chip
	int hoveredControl = 0;     // 0 none, 1 reveal, 2 add channel
	int hoveredRow = -1;        // assignment index under the pointer

	QLineEdit* editor = nullptr;
	int editRow = -1;
	int editSummand = -1;
	QLineEdit* channelEditor = nullptr;

	int blockH = 46;
	int gap = 8;
	int controlH = 30;
};

class BlockChipRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent) override;
	const char* id() const override { return "block-chip"; }
};
