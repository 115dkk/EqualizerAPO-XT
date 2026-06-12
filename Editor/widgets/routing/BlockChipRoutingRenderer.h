/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft skin's Copy renderer: each output is a soft, rounded "equation block"
	reading like VSL = 0.86·L − 0.5·R, built from friendly channel chips and
	factor chips. Tactile and approachable, matching the Soft skin philosophy.
*/

#pragma once

#include <QLineEdit>
#include <QVector>

#include "IRoutingRenderer.h"
#include "filters/CopyFilter.h"

class BlockChipView : public RoutingView
{
	Q_OBJECT

public:
	BlockChipView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, QWidget* parent);

	std::vector<Assignment> assignments() const override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
	struct Hit { int row = 0; int summand = 0; QRect rect; };
	struct AddHit { int row = 0; QRect rect; };
	void commitEditor();
	void showAddMenu(int row, const QPoint& globalPos);

	std::vector<Assignment> workingAssignments;
	// Device channel layout, offered by the per-block [+] chip menu.
	std::vector<std::wstring> deviceChannels;
	QVector<Hit> hits;
	QVector<AddHit> addHits;
	QLineEdit* editor = nullptr;
	int editRow = -1;
	int editSummand = -1;

	int blockH = 46;
	int gap = 8;
};

class BlockChipRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, QWidget* parent) override;
	const char* id() const override { return "block-chip"; }
};
