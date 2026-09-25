/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QToolButton>
#include <QWidget>

#include "Editor/FilterTable.h"
#include "Editor/widgets/CommandRowFrame.h"
#include "Editor/widgets/FilterCardModel.h"

class QScrollArea;
class ElidedLabel;
class RoutingView;

class FilterCardRow : public QWidget
{
	Q_OBJECT

public:
	FilterCardRow(FilterTable* table, int number, FilterTable::Item* item, IFilterGUI* gui,
		FilterCardDescriptor descriptor, QWidget* parent = nullptr);
	void setChannelFlow(const ChannelFlowAtLine& flow);

	QRect getHeaderRect() const;
	void editText();
	// Pull the document's current selection/focus facts into the skin-owned
	// frame state. FilterTable calls this after every selection mutation; an
	// ordinary repaint is intentionally not responsible for changing state.
	void syncVisualState();
	// In-place refresh of the 1-based row number and the channel/If scope after
	// an incremental row insert/remove above this row, so shifted rows do not
	// need to be rebuilt. Updates exactly what the constructor derived from its
	// number/scope arguments.
	void updateRowPosition(int rowNumber, FilterCardRowScope scope);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;
	// Fixes an editor scroll wrapper to its content's height, bounded to
	// 24..600 px so a runaway body cannot swallow the table. Returns whether
	// the height changed. Shared with editors that resize their own body
	// (VelvetCardEditor's advanced panel).
	static bool fitEditorScrollHeight(QScrollArea* scroll, int contentHeight);

protected:
	void paintEvent(QPaintEvent*) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
	void updateModel();
	void addAbove();
	void removeThis();
	void editTextToggled(bool checked);
	void lineEditingFinished();
	void enabledToggled(bool checked);
	void expandedToggled(bool checked);
	void routingEdited();

private:
	void watchPointerSelection(QWidget* root);
	// The body wrapper: the styled editor container and its width-pinned
	// scroll area around the content createContent builds (as a child of the
	// container), added to the body stack as the current page.
	void mountEditorBody(const std::function<QWidget*(QWidget* editorContainer)>& createContent);
	void watchEditorScroll(QScrollArea* scroll);
	void syncEditorScrollHeight(QScrollArea* scroll);
	void applyDescriptor();
	void rebuildSummary();
	void setEditing(bool editing);
	void buildChannelBadges(const QStringList& channels);
	CommandRowInfo currentRowInfo() const;
	QString uncommentedLine() const;
	// Indent units for the outer margin. Branch/tail rows of the If family
	// (ElseIf/Else/EndIf) follow the active skin's layout policy
	// (ISkin::logicSiblingsIndentAsMembers): a skin that runs a scope lane
	// down the gutter indents them with the members.
	int rowIndentUnits() const;

	FilterTable* table = nullptr;
	FilterTable::Item* item = nullptr;
	IFilterGUI* gui = nullptr;
	FilterCardDescriptor descriptor;
	// 1-based document row, kept current by updateRowPosition; indexes the
	// table's analysis load facts (loadTraceFactsForRow) for currentRowInfo.
	int rowNumber = 0;

	CommandRowFrame* cardFrame = nullptr;
	CommandRowInfo visualInfo;
	QWidget* headerWidget = nullptr;
	QLabel* numberLabel = nullptr;
	QLabel* typeBadge = nullptr;
	ElidedLabel* titleLabel = nullptr;
	ElidedLabel* summaryLabel = nullptr;
	QWidget* channelBadgeContainer = nullptr;
	QHBoxLayout* channelBadgeLayout = nullptr;
	QToolButton* enabledButton = nullptr;
	QToolButton* expandButton = nullptr;
	QToolButton* addButton = nullptr;
	QToolButton* removeButton = nullptr;
	QToolButton* editButton = nullptr;
	QStackedWidget* bodyStack = nullptr;
	QLineEdit* lineEdit = nullptr;
	RoutingView* routingView = nullptr;
	QStringList renderedChannelBadges;
	bool editingDone = false;
};
