/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Minimal skin's Copy renderer: a monospace, terminal-like sequential step
	list. Each output is one row "# │ Dest ← Sources" with explicit + / − signs,
	×N gain factors and an INV marker for phase inversion. This is the academic,
	unambiguous text+number form. The listing folds like a pager: only the
	steps the command involves are listed, the rest of the device layout sits
	behind a fold line ("[+N CH]"), and the closing prompt is the add-channel
	entry - clicking it lets a new virtual channel be typed at the prompt.
*/

#pragma once

#include <QLineEdit>
#include <QStringList>
#include <QVector>

#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/RoutingFold.h"
#include "filters/CopyFilter.h"

class StepListView : public RoutingView
{
	Q_OBJECT

public:
	StepListView(const std::vector<Assignment>& assignments,
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
	// Device channel layout, offered by the per-row [+] source menu.
	std::vector<std::wstring> deviceChannels;
	// Fixed-source mode (MultiConvolution): the [+] menu offers only
	// portModel.fixedSources and factors are locked to unity (a double-click
	// removes the source instead of editing a gain). The fixed sources stay
	// available while the target steps use the shared channel fold.
	RoutingPortModel portModel;

	// Channel fold: the listing shows fold.visibleRows only; pinned channels
	// stay listed while their sum is empty.
	bool channelsExpanded = false;
	QStringList pinnedChannels;
	RoutingFold::Fold fold;

	QVector<Hit> hits;       // factor / channel chip hit-rects, rebuilt each paint
	QVector<AddHit> addHits; // per-row [+] hit-rects, rebuilt each paint
	QVector<AddHit> removeHits; // hovered virtual dest [x] targets
	QRect foldRect;          // the fold line's bracket target
	QRect promptRect;        // the prompt line (add-channel entry)
	int hoveredControl = 0;  // 0 none, 1 fold bracket, 2 prompt
	int hoveredRow = -1;     // display row under the pointer (assignment index)

	QLineEdit* editor = nullptr;
	int editRow = -1;
	int editSummand = -1;
	QLineEdit* channelEditor = nullptr;

	int rowH = 30;
	int headerH = 22;
};

class StepListRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent) override;
	const char* id() const override { return "step-list"; }
};
