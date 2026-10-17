/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The interaction half of the sum-shaped routing views: Minimal's step
	list and Soft's equation blocks. Both draw one row per output target,
	reading as that target's source sum; a per-row add target offers the
	sources the sum does not hold yet, a double-click on a summand opens an
	inline editor, the rows fold to the channels the command involves behind
	a reveal control, an add control opens the add-channel field, and a
	hovered virtual row offers a remove target. The two skins differ in
	what a row looks like and in which editor a summand opens, so a derived
	view supplies paintEvent, sizeHint, the geometry virtuals below and its
	SummandEditor. The sourceCandidates/connectSource hooks the routing-edit
	gate drives are answered here for both.
*/

#pragma once

#include <vector>
#include <QRect>
#include <QStringList>
#include <QVector>

#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/RoutingChannelEditor.h"
#include "Editor/widgets/routing/RoutingFold.h"
#include "filters/CopyCommand.h"
#include "Editor/SkinTokens.h"

class QLineEdit;
class QStringListModel;

class SumRoutingViewBase : public RoutingView
{
	Q_OBJECT

public:
	std::vector<Assignment> assignments() const override;
	void galleryShowcase(const QString& state) override;
	QStringList sourceCandidates(const QString& target) const override;
	bool connectSource(const QString& target, const QString& source) override;
	QSize minimumSizeHint() const override;

protected:
	// What a double-click on a summand opens. SourceToken holds the summand
	// as the line writes it ("0.5*L"), so retyping the token changes channel
	// and factor at once (RoutingFold::parseSourceToken), and completes
	// channel names from the row's add-menu candidates; it also answers the
	// gallery's "editSource:" state. Factor holds the gain alone and reads
	// an offered channel name as that channel at unity
	// (RoutingGridModel::commitChip); with factors locked the double-click
	// removes the summand instead.
	enum class SummandEditor
	{
		SourceToken,
		Factor
	};

	// Hit-rects paintEvent lays out: a summand's editable parts, and per-row
	// targets (the add target, the remove target).
	struct Hit { int row = 0; int summand = 0; QRect rect; };
	struct AddHit { int row = 0; QRect rect; };

	// The editor names are the skin sheet's selectors for the two fields.
	// The derived constructor must end with refold(): that reaches
	// sizeHint(), which is not the skin's yet while this constructor runs.
	SumRoutingViewBase(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens, SummandEditor summandEditorKind,
		const QString& summandEditorName, const QString& channelEditorName);

	void paintEvent(QPaintEvent* event) override = 0;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

	// The display slot (an index into fold.visibleRows) at pos's height, or
	// -1 above the first row; a slot past the last row is ignored.
	virtual int slotAt(const QPoint& pos) const = 0;
	// Where the summand editor sits. span is the union of the summand's
	// hit-rects (null when none is painted yet). minimumWidth is what the
	// SourceToken editor's own face needs to show a full "-0.000dB*LFE"
	// token, and 0 for the Factor editor.
	virtual QRect summandEditorRect(const QRect& span, int minimumWidth) const = 0;
	// Where the add-channel field opens; addChannelRect is null before the
	// first paint.
	virtual QRect channelEditorRect() const = 0;

	// Refolds the seeded rows and resizes to the new hint.
	void refold();
	// The open summand editor's geometry while it edits (row, summand), a
	// null rect otherwise: the step list flows the rest of the row around it.
	QRect openSummandEditorRect(int row, int summand) const;
	bool channelEditorOpen() const;

	const SkinTokens skinTokens;
	std::vector<Assignment> workingAssignments;
	// Device channel layout, offered by the per-row add menu.
	std::vector<std::wstring> deviceChannels;
	// Fixed-source mode (MultiConvolution): the add menu offers only
	// portModel.fixedSources, the SourceToken editor reads a bare integer as
	// a port and factors may be locked to unity. The fixed sources stay
	// available while the target rows use the shared channel fold.
	RoutingPortModel portModel;

	// Channel fold: only fold.visibleRows are drawn; pinned channels keep
	// their row while their sum is empty.
	bool channelsExpanded = false;
	QStringList pinnedChannels;
	RoutingFold::Fold fold;

	QVector<Hit> hits;          // summand hit-rects, rebuilt each paint
	QVector<AddHit> addHits;    // per-row add targets, rebuilt each paint
	QVector<AddHit> removeHits; // hovered virtual target's remove target
	QRect revealRect;           // the fold's reveal / fold-back control
	QRect addChannelRect;       // the add-channel control
	int hoveredControl = 0;     // 0 none, 1 reveal, 2 add channel
	int hoveredRow = -1;        // assignment index under the pointer

private:
	QStringList sourceCandidatesForRow(int row) const;
	bool addSourceToRow(int row, const QString& channel);
	void showAddMenu(int row, const QPoint& globalPos);
	void openSummandEditor(int row, int summand);
	void commitSummandEditor();
	void openChannelEditor();
	void commitChannelEditor();

	const SummandEditor summandEditorKind;
	const QString summandEditorName;
	QLineEdit* summandEditor = nullptr;
	// The SourceToken editor's completion list, refilled per opening.
	QStringListModel* sourceCompletions = nullptr;
	int editRow = -1;
	int editSummand = -1;
	RoutingChannelEditor channelEditor;
};
