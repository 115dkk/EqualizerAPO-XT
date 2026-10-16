/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SumRoutingViewBase.h"

#include <QCompleter>
#include <QFontMetrics>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QStringListModel>

#include "Editor/widgets/routing/CopyRoutingAdapter.h"
#include "Editor/widgets/routing/RoutingGridModel.h"

using std::vector;

SumRoutingViewBase::SumRoutingViewBase(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens, SummandEditor summandEditorKind,
	const QString& summandEditorName, const QString& channelEditorName)
	: RoutingView(parent), skinTokens(tokens),
	// Seed every device channel as a row so an emptied Copy can be refilled
	// from the GUI; rows whose source sum stays empty are skipped by the
	// serializer and never reach the config line. The fold decides which
	// seeded rows are actually drawn.
	workingAssignments(CopyRoutingAdapter::seedTargets(assignments, channelNames)),
	deviceChannels(channelNames),
	portModel(portModel),
	// Targets the command referenced keep their row for the whole session,
	// even if their last source is removed.
	pinnedChannels(RoutingFold::referencedTargets(assignments)),
	summandEditorKind(summandEditorKind),
	summandEditorName(summandEditorName),
	channelEditor(channelEditorName, false)
{
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
}

std::vector<Assignment> SumRoutingViewBase::assignments() const
{
	return workingAssignments;
}

void SumRoutingViewBase::refold()
{
	fold = RoutingFold::fold(workingAssignments, deviceChannels, pinnedChannels,
		channelsExpanded, portModel.fixedSources);
	syncSizeToHint();
	update();
}

void SumRoutingViewBase::galleryShowcase(const QString& state)
{
	if (state == QLatin1String("expanded"))
	{
		channelsExpanded = true;
		refold();
	}
	else if (state == QLatin1String("addChannel"))
	{
		openChannelEditor();
		channelEditor.setText(QStringLiteral("VS"));
	}
	else if (state.startsWith(QLatin1String("editSource:"))
		&& summandEditorKind == SummandEditor::SourceToken)
	{
		// The gate's stand-in for a double-click: the named target's first
		// summand. paintEvent lays out the hit-rects the editor sits on, so
		// the view must have painted once (the caller shows it first).
		const int row = RoutingGridModel::rowIndexOf(workingAssignments, state.mid(11));
		if (row >= 0 && !workingAssignments[row].sourceSum.empty())
			openSummandEditor(row, 0);
	}
}

QStringList SumRoutingViewBase::sourceCandidates(const QString& target) const
{
	return sourceCandidatesForRow(RoutingGridModel::rowIndexOf(workingAssignments, target));
}

bool SumRoutingViewBase::connectSource(const QString& target, const QString& source)
{
	return addSourceToRow(RoutingGridModel::rowIndexOf(workingAssignments, target), source);
}

QSize SumRoutingViewBase::minimumSizeHint() const
{
	return sizeHint();
}

QRect SumRoutingViewBase::openSummandEditorRect(int row, int summand) const
{
	if (summandEditor != nullptr && summandEditor->isVisible() && editRow == row && editSummand == summand)
		return summandEditor->geometry();
	return QRect();
}

bool SumRoutingViewBase::channelEditorOpen() const
{
	return channelEditor.isOpen();
}

void SumRoutingViewBase::mousePressEvent(QMouseEvent* event)
{
	for (const AddHit& h : removeHits)
	{
		if (h.rect.contains(event->pos()))
		{
			const QString channel = QString::fromStdWString(workingAssignments[h.row].targetChannel);
			const bool changed = RoutingGridModel::removeChannel(workingAssignments, pinnedChannels, channel);
			refold();
			if (changed)
				emit routingChanged();
			return;
		}
	}
	for (const AddHit& h : addHits)
	{
		if (h.rect.contains(event->pos()))
		{
			showAddMenu(h.row, mapToGlobal(h.rect.bottomLeft()));
			return;
		}
	}
	if (!revealRect.isNull() && revealRect.contains(event->pos()))
	{
		channelsExpanded = !channelsExpanded;
		refold();
		return;
	}
	if (!addChannelRect.isNull() && addChannelRect.contains(event->pos()))
	{
		openChannelEditor();
		return;
	}
	RoutingView::mousePressEvent(event);
}

void SumRoutingViewBase::mouseMoveEvent(QMouseEvent* event)
{
	int control = 0;
	if (!revealRect.isNull() && revealRect.contains(event->pos()))
		control = 1;
	else if (!addChannelRect.isNull() && addChannelRect.contains(event->pos()))
		control = 2;

	int row = -1;
	const int slot = slotAt(event->pos());
	if (slot >= 0 && slot < fold.visibleRows.size())
		row = fold.visibleRows[slot];

	if (control != hoveredControl || row != hoveredRow)
	{
		hoveredControl = control;
		hoveredRow = row;
		setCursor(control != 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
		update();
	}
	RoutingView::mouseMoveEvent(event);
}

void SumRoutingViewBase::leaveEvent(QEvent* event)
{
	if (hoveredControl != 0 || hoveredRow >= 0)
	{
		hoveredControl = 0;
		hoveredRow = -1;
		update();
	}
	RoutingView::leaveEvent(event);
}

QStringList SumRoutingViewBase::sourceCandidatesForRow(int row) const
{
	QStringList candidates;
	if (row < 0 || row >= (int)workingAssignments.size())
		return candidates;

	auto inSum = [this, row](const QString& channel) {
		for (const Assignment::Summand& s : workingAssignments[row].sourceSum)
			if (QString::fromStdWString(s.channel).compare(channel, Qt::CaseInsensitive) == 0)
				return true;
		return false;
	};
	auto addCandidate = [&](const QString& channel) {
		if (channel.isEmpty() || channel == QLatin1String(" ")
			|| inSum(channel) || candidates.contains(channel, Qt::CaseInsensitive))
			return;
		candidates.append(channel);
	};
	if (portModel.fixedSourceMode())
	{
		// Fixed sources (IR file channels): the port list is the whole menu.
		for (const QString& port : portModel.fixedSources)
			addCandidate(port);
	}
	else
	{
		for (const std::wstring& name : deviceChannels)
			addCandidate(QString::fromStdWString(name));
		// Channels the command references elsewhere (e.g. virtual channels) stay
		// available even when the device layout is unknown.
		for (const Assignment& other : workingAssignments)
		{
			addCandidate(QString::fromStdWString(other.targetChannel));
			for (const Assignment::Summand& s : other.sourceSum)
				addCandidate(QString::fromStdWString(s.channel));
		}
	}
	return candidates;
}

bool SumRoutingViewBase::addSourceToRow(int row, const QString& channel)
{
	if (row < 0 || row >= (int)workingAssignments.size() || channel.isEmpty())
		return false;
	for (const Assignment::Summand& s : workingAssignments[row].sourceSum)
		if (QString::fromStdWString(s.channel).compare(channel, Qt::CaseInsensitive) == 0)
			return false;

	Assignment::Summand s;
	s.factor = 1.0;
	s.isDecibel = false;
	s.channel = channel.toStdWString();
	workingAssignments[row].sourceSum.push_back(s);
	refold();
	emit routingChanged();
	return true;
}

void SumRoutingViewBase::showAddMenu(int row, const QPoint& globalPos)
{
	const QStringList candidates = sourceCandidatesForRow(row);
	if (candidates.isEmpty())
		return;

	QMenu menu(this);
	for (const QString& channel : candidates)
		menu.addAction(channel);
	const QAction* chosen = menu.exec(globalPos);
	if (chosen == nullptr)
		return;
	addSourceToRow(row, chosen->text());
}

void SumRoutingViewBase::mouseDoubleClickEvent(QMouseEvent* event)
{
	int row = -1, summand = -1;
	for (const Hit& h : hits)
	{
		if (h.rect.contains(event->pos()))
		{
			row = h.row;
			summand = h.summand;
			break;
		}
	}
	if (row < 0)
		return;

	if (summandEditorKind == SummandEditor::Factor && !portModel.allowFactors)
	{
		// Without factors the only chip edit is removal.
		Assignment& a = workingAssignments[row];
		if (summand >= 0 && summand < (int)a.sourceSum.size())
		{
			a.sourceSum.erase(a.sourceSum.begin() + summand);
			refold();
			emit routingChanged();
		}
		return;
	}
	openSummandEditor(row, summand);
}

void SumRoutingViewBase::openSummandEditor(int row, int summand)
{
	if (row < 0 || row >= (int)workingAssignments.size()
		|| summand < 0 || summand >= (int)workingAssignments[row].sourceSum.size())
		return;

	commitSummandEditor();
	editRow = row;
	editSummand = summand;

	const bool sourceToken = summandEditorKind == SummandEditor::SourceToken;
	if (summandEditor == nullptr)
	{
		summandEditor = new QLineEdit(this);
		summandEditor->setObjectName(summandEditorName);
		if (sourceToken)
		{
			// Channel-name completion from the row's own candidate list: the
			// terminal's tab-completion, so the hint the add menu carries is
			// at hand while typing too.
			sourceCompletions = new QStringListModel(summandEditor);
			QCompleter* completer = new QCompleter(sourceCompletions, summandEditor);
			completer->setCaseSensitivity(Qt::CaseInsensitive);
			completer->setCompletionMode(QCompleter::PopupCompletion);
			summandEditor->setCompleter(completer);
		}
		else
		{
			summandEditor->setAlignment(Qt::AlignCenter);
		}
		connect(summandEditor, &QLineEdit::editingFinished, this, &SumRoutingViewBase::commitSummandEditor);
	}

	QRect span;
	for (const Hit& h : hits)
		if (h.row == row && h.summand == summand)
			span |= h.rect;
	int minimumWidth = 0;
	if (sourceToken)
	{
		// The editor's own face (the sheet pins the listing's mono face on
		// it), polished first so a fresh widget does not measure with the
		// default. The field must show what it holds.
		summandEditor->ensurePolished();
		const QFontMetrics fm = summandEditor->fontMetrics();
		minimumWidth = fm.horizontalAdvance(QStringLiteral("-0.000dB*LFE")) + 24;
	}
	summandEditor->setGeometry(summandEditorRect(span, minimumWidth));

	const Assignment::Summand& s = workingAssignments[row].sourceSum[summand];
	if (sourceToken)
	{
		QStringList completions = sourceCandidatesForRow(row);
		completions.prepend(QString::fromStdWString(s.channel));
		sourceCompletions->setStringList(completions);
		summandEditor->setText(RoutingFold::sourceToken(s));
	}
	else
	{
		summandEditor->setText(s.isDecibel ? QStringLiteral("%1dB").arg(s.factor) : QString::number(s.factor));
	}
	summandEditor->show();
	summandEditor->setFocus();
	summandEditor->selectAll();
	// Reflow the row around the editor (the step list's paintEvent).
	update();
}

void SumRoutingViewBase::commitSummandEditor()
{
	if (summandEditor == nullptr || !summandEditor->isVisible() || editRow < 0)
		return;

	const int row = editRow, si = editSummand;
	editRow = editSummand = -1;
	const QString raw = summandEditor->text().trimmed();
	summandEditor->hide();
	// The row flowed around the editor; flow it back even when nothing
	// changes below.
	update();

	const bool changed = summandEditorKind == SummandEditor::SourceToken
		? RoutingGridModel::commitSource(workingAssignments, row, si, raw,
			portModel.fixedSourceMode(), portModel.allowFactors)
		: RoutingGridModel::commitChip(workingAssignments, row, si, raw,
			RoutingGridModel::sourceChannels(workingAssignments, deviceChannels, portModel.fixedSources));
	if (!changed)
		return;
	refold();
	emit routingChanged();
}

void SumRoutingViewBase::openChannelEditor()
{
	channelEditor.open(this, &SumRoutingViewBase::commitChannelEditor, channelEditorRect());
}

void SumRoutingViewBase::commitChannelEditor()
{
	QString name;
	if (!channelEditor.take(name))
		return;
	if (RoutingGridModel::addChannel(workingAssignments, pinnedChannels, name))
		refold();
}
