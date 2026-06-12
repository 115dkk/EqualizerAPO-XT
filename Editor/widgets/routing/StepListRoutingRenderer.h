/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Minimal skin's Copy renderer: a monospace, terminal-like sequential step
	list. Each output is one row "# │ Dest ← Sources" with explicit + / − signs,
	×N gain factors and an INV marker for phase inversion. This is the academic,
	unambiguous text+number form (adapted from the redesign mock-up's
	step-based CopyExpanded view).
*/

#pragma once

#include <QLineEdit>
#include <QVector>

#include "IRoutingRenderer.h"
#include "filters/CopyFilter.h"

class StepListView : public RoutingView
{
	Q_OBJECT

public:
	StepListView(const std::vector<Assignment>& assignments,
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
	// Device channel layout, offered by the per-row [+] source menu.
	std::vector<std::wstring> deviceChannels;
	QVector<Hit> hits;       // factor / channel chip hit-rects, rebuilt each paint
	QVector<AddHit> addHits; // per-row [+] hit-rects, rebuilt each paint
	QLineEdit* editor = nullptr;
	int editRow = -1;
	int editSummand = -1;

	int rowH = 30;
	int headerH = 22;
};

class StepListRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, QWidget* parent) override;
	const char* id() const override { return "step-list"; }
};
