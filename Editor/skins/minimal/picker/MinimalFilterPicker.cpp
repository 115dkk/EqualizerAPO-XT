/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MinimalFilterPicker.h"

#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

namespace
{
// Dense terminal metrics: a 20px entry line at 9pt mono, captions slightly
// taller so the hairline underline gets one clear pixel row of its own.
int entryRowHeight()
{
	return GUIHelper::scale(20.0);
}

int sectionRowHeight()
{
	return GUIHelper::scale(26.0);
}

int sidePadding()
{
	return GUIHelper::scale(10.0);
}

QFont pickerMonoFont(const SkinTokens& tokens, double pointSize, bool bold = false)
{
	QFont font(tokens.monoFontFamily);
	font.setPointSizeF(pointSize);
	font.setBold(bold);
	return font;
}
}

// ── MinimalPickerIndexList ───────────────────────────────────────────────────

MinimalPickerIndexList::MinimalPickerIndexList(const SkinTokens& tokens, QWidget* parent)
	: QWidget(parent),
	  skinTokens(tokens)
{
	setMouseTracking(true);
}

void MinimalPickerIndexList::setRows(const QList<Row>& rows)
{
	rowList = rows;
	rowTops.clear();
	rowTops.reserve(rowList.size());
	int y = GUIHelper::scale(2.0);
	for (const Row& row : rowList)
	{
		rowTops.append(y);
		y += row.entryIndex < 0 ? sectionRowHeight() : entryRowHeight();
	}
	contentHeight = y + GUIHelper::scale(4.0);
	// Inside a widgetResizable scroll area the minimum height is what makes
	// the viewport scroll instead of squashing the rows.
	setMinimumHeight(contentHeight);
	hoverRow = -1;
	updateGeometry();
	update();
}

void MinimalPickerIndexList::setSelectedEntry(int entryIndex)
{
	if (selectedEntryIndex == entryIndex)
		return;
	selectedEntryIndex = entryIndex;
	update();
}

int MinimalPickerIndexList::rowOfEntry(int entryIndex) const
{
	if (entryIndex < 0)
		return -1;
	for (int i = 0; i < rowList.size(); i++)
	{
		if (rowList[i].entryIndex == entryIndex)
			return i;
	}
	return -1;
}

QRect MinimalPickerIndexList::rowRect(int row) const
{
	if (row < 0 || row >= rowList.size())
		return QRect();
	const int rowHeight = rowList[row].entryIndex < 0 ? sectionRowHeight() : entryRowHeight();
	return QRect(0, rowTops[row], width(), rowHeight);
}

QSize MinimalPickerIndexList::sizeHint() const
{
	return QSize(GUIHelper::scale(380.0), contentHeight);
}

int MinimalPickerIndexList::rowAt(const QPoint& pos) const
{
	for (int i = 0; i < rowList.size(); i++)
	{
		if (rowRect(i).contains(pos))
			return i;
	}
	return -1;
}

void MinimalPickerIndexList::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	const SkinTokens& t = skinTokens;
	painter.fillRect(rect(), QColor(t.background));

	QFont entryFont = pickerMonoFont(skinTokens, 9.0);
	QFont captionFont = pickerMonoFont(skinTokens, 7.5, true);
	captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
	const QFontMetrics entryMetrics(entryFont);

	const int pad = sidePadding();
	const int numberGap = GUIHelper::scale(8.0);

	if (rowList.isEmpty())
	{
		// A query that matches nothing: the index answers with one quiet
		// status line (the counter above already reads 0/NN). Secondary ink,
		// no box, no icon.
		painter.setFont(entryFont);
		painter.setPen(QColor(t.mutedText));
		painter.drawText(QRect(pad, GUIHelper::scale(2.0), width() - 2 * pad, entryRowHeight()),
			Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("NO MATCH"));
		return;
	}

	for (int i = 0; i < rowList.size(); i++)
	{
		const Row& row = rowList[i];
		const QRect r = rowRect(i);
		if (!r.intersects(event->rect()))
			continue;

		if (row.entryIndex < 0)
		{
			// Section caption: uppercase letter-spaced mono over a full-width
			// hairline rule.
			painter.setFont(captionFont);
			painter.setPen(QColor(t.mutedText));
			painter.drawText(r.adjusted(pad, GUIHelper::scale(5.0), -pad, -GUIHelper::scale(3.0)),
				Qt::AlignVCenter | Qt::AlignLeft, row.text);
			painter.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), QColor(t.border));
		}
		else
		{
			const bool selected = row.entryIndex == selectedEntryIndex;
			if (selected)
			{
				// Inverted block: the line trades foreground for background.
				painter.fillRect(r, QColor(t.text));
			}
			else if (i == hoverRow)
			{
				painter.fillRect(r, QColor(t.cardHover));
			}

			painter.setFont(entryFont);
			QColor numberColor = selected ? QColor(t.background) : QColor(t.mutedText);
			if (selected)
				numberColor.setAlpha(170);
			painter.setPen(numberColor);
			const int numberWidth = entryMetrics.horizontalAdvance(row.number);
			painter.drawText(QRect(r.left() + pad, r.top(), numberWidth, r.height()),
				Qt::AlignVCenter | Qt::AlignLeft, row.number);

			painter.setPen(selected ? QColor(t.background) : QColor(t.text));
			const int nameLeft = r.left() + pad + numberWidth + numberGap;
			const int nameWidth = r.right() - pad - nameLeft;
			painter.drawText(QRect(nameLeft, r.top(), nameWidth, r.height()),
				Qt::AlignVCenter | Qt::AlignLeft,
				entryMetrics.elidedText(row.text, Qt::ElideRight, nameWidth));
		}
	}
}

void MinimalPickerIndexList::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
		return;
	const int row = rowAt(event->pos());
	if (row < 0 || rowList[row].entryIndex < 0)
		return;
	selectedEntryIndex = rowList[row].entryIndex;
	update();
	if (onEntryActivated)
		onEntryActivated(rowList[row].entryIndex);
}

void MinimalPickerIndexList::mouseMoveEvent(QMouseEvent* event)
{
	int row = rowAt(event->pos());
	if (row >= 0 && rowList[row].entryIndex < 0)
		row = -1;
	if (row != hoverRow)
	{
		hoverRow = row;
		update();
	}
}

void MinimalPickerIndexList::leaveEvent(QEvent* event)
{
	Q_UNUSED(event);
	if (hoverRow != -1)
	{
		hoverRow = -1;
		update();
	}
}

void MinimalPickerIndexList::hoverFirstEntryForGallery()
{
	// The offscreen gallery cannot move a real cursor, so the hover state is
	// staged directly. The selection block usually sits on the first line;
	// hovering it would vanish under the inverted fill, so take the first
	// line that is not the cursor.
	for (int i = 0; i < rowList.size(); i++)
	{
		if (rowList[i].entryIndex >= 0 && rowList[i].entryIndex != selectedEntryIndex)
		{
			hoverRow = i;
			update();
			return;
		}
	}
}

// ── MinimalFilterPickerView ──────────────────────────────────────────────────

MinimalFilterPickerView::MinimalFilterPickerView(const SkinTokens& tokens, QWidget* parent)
	: FilterPickerView(parent),
	  skinTokens(tokens)
{
	setObjectName(QStringLiteral("MinimalFilterPicker"));

	QVBoxLayout* layout = new QVBoxLayout(this);
	// 1px margins keep the children inside the hairline frame painted by
	// paintEvent (the popup container itself is chromeless).
	layout->setContentsMargins(1, 1, 1, 1);
	layout->setSpacing(0);

	// Query line: a bare ">" prompt, a frameless line edit and a match
	// counter; the hairline under the row comes from the skin QSS.
	QWidget* header = new QWidget(this);
	header->setObjectName(QStringLiteral("MinimalPickerHeader"));
	header->setAttribute(Qt::WA_StyledBackground, true);
	QHBoxLayout* headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(sidePadding(), GUIHelper::scale(6.0), sidePadding(), GUIHelper::scale(6.0));
	headerLayout->setSpacing(GUIHelper::scale(8.0));

	QLabel* prompt = new QLabel(QStringLiteral(">"), header);
	prompt->setObjectName(QStringLiteral("MinimalPickerPrompt"));
	headerLayout->addWidget(prompt);

	queryEdit = new QLineEdit(header);
	queryEdit->setObjectName(QStringLiteral("MinimalPickerQuery"));
	queryEdit->setFrame(false);
	queryEdit->installEventFilter(this);
	connect(queryEdit, &QLineEdit::textChanged, this, [this](const QString& query) {
		setPickerQuery(query.trimmed());
		rebuildIndex();
	});
	headerLayout->addWidget(queryEdit, 1);

	countLabel = new QLabel(header);
	countLabel->setObjectName(QStringLiteral("MinimalPickerCount"));
	headerLayout->addWidget(countLabel);

	layout->addWidget(header);

	scrollArea = new QScrollArea(this);
	scrollArea->setObjectName(QStringLiteral("MinimalPickerScroll"));
	scrollArea->setFrameShape(QFrame::NoFrame);
	scrollArea->setWidgetResizable(true);
	scrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	indexList = new MinimalPickerIndexList(skinTokens, scrollArea);
	indexList->onEntryActivated = [this](int entryIndex)
	{
		emit entryChosen(entryIndex);
	};
	scrollArea->setWidget(indexList);
	layout->addWidget(scrollArea, 1);

	// Key legend: the full keyboard grammar in one man-page line - letters
	// filter, digits jump, arrows move, Return inserts. Plain text, hairline
	// above (QSS); the arrows are built from code points so the source stays
	// pure ASCII.
	const QString dot = QStringLiteral(" %1 ").arg(QChar(0x00B7));
	QLabel* legend = new QLabel(
		QStringLiteral("A-Z FILTER") + dot + QStringLiteral("NN JUMP") + dot
		+ QString(QChar(0x2191)) + QChar(0x2193) + QStringLiteral(" MOVE") + dot
		+ QStringLiteral("RET INSERT"), this);
	legend->setObjectName(QStringLiteral("MinimalPickerLegend"));
	layout->addWidget(legend);

	// The host calls view->setFocus(); typing must land in the query line.
	setFocusProxy(queryEdit);
	setMinimumWidth(GUIHelper::scale(360.0));
	setMaximumHeight(GUIHelper::scale(460.0));
}

void MinimalFilterPickerView::entriesChanged()
{
	setPickerQuery(queryEdit->text().trimmed());
	rebuildDisplayNumbers();
	rebuildIndex();
	queryEdit->setFocus();
}

void MinimalFilterPickerView::galleryShowcase(GalleryShowcase kind)
{
	if (kind == GalleryShowcase::PhaseAndTimeSearch)
	{
		// Where Delay and the two all-pass sections went. The category is
		// part of what the shared predicate searches, so one term returns
		// the whole group.
		queryEdit->setText(QStringLiteral("phase"));
		return;
	}
	if (kind == GalleryShowcase::EmptySearch)
	{
		// A term no template matches: the index prints its NO MATCH line and
		// the counter reads 0/NN.
		queryEdit->setText(QStringLiteral("zzzz"));
		return;
	}
	queryEdit->clear();
	indexList->hoverFirstEntryForGallery();
}

QString MinimalFilterPickerView::sectionKey(const FilterPickerEntry& entry) const
{
	return entry.path.isEmpty() ? tr("General") : entry.path.join(QStringLiteral(" / "));
}

void MinimalFilterPickerView::rebuildDisplayNumbers()
{
	// Numbers are page coordinates: walk the entries exactly the way the
	// resting index lays them out (sections coalesced by first appearance,
	// original order within a section) and number that order 1..N. The
	// assignment never changes afterwards: filtering hides lines but keeps
	// their printed numbers, and a digit jump always lands on the number the
	// user read off the page.
	QStringList sectionOrder;
	QHash<QString, QList<int>> sectionEntries;
	for (int i = 0; i < pickerEntries().size(); i++)
	{
		const QString section = sectionKey(pickerEntries()[i]);
		if (!sectionEntries.contains(section))
			sectionOrder.append(section);
		sectionEntries[section].append(i);
	}

	displayOrder.clear();
	displayOrder.reserve(pickerEntries().size());
	displayNumbers.fill(0, pickerEntries().size());
	for (const QString& section : sectionOrder)
	{
		for (int i : sectionEntries.value(section))
		{
			displayOrder.append(i);
			displayNumbers[i] = displayOrder.size();
		}
	}
}

QSize MinimalFilterPickerView::sizeHint() const
{
	// The layout-driven hint grows with the full index; cap it here so the
	// host's adjustSize() yields a dropdown, not a tower.
	QSize hint = FilterPickerView::sizeHint();
	hint.setWidth(GUIHelper::scale(380.0));
	hint.setHeight(qMin(hint.height(), GUIHelper::scale(460.0)));
	return hint;
}

void MinimalFilterPickerView::rebuildIndex()
{
	const QString& query = pickerQuery();

	// Pure digits = index jump (the numbers are the menu); anything else is a
	// plain substring filter over section, name and config line.
	bool jumpMode = !query.isEmpty();
	for (const QChar& c : query)
	{
		if (!c.isDigit())
		{
			jumpMode = false;
			break;
		}
	}
	// Coalesce by path (first-appearance order): one caption per category.
	// Factories may revisit a category, and a repeated caption would read as
	// corruption in an index. This walk revisits the entries in their
	// original order, so the visible lines come out exactly in the page
	// order rebuildDisplayNumbers() numbered.
	QStringList sectionOrder;
	QHash<QString, QList<int>> sectionEntries;
	for (int i = 0; i < pickerEntries().size(); i++)
	{
		const FilterPickerEntry& entry = pickerEntries()[i];
		const QString section = sectionKey(entry);

		if (!jumpMode && !filterPickerMatches(entry, section, query))
			continue;

		if (!sectionEntries.contains(section))
			sectionOrder.append(section);
		sectionEntries[section].append(i);
	}

	const int digits = qMax(2, QString::number(pickerEntries().size()).size());
	QList<MinimalPickerIndexList::Row> rows;
	int firstVisible = -1;
	int visibleCount = 0;
	for (const QString& section : sectionOrder)
	{
		rows.append({ QString(), section.toUpper(), -1 });
		for (int i : sectionEntries.value(section))
		{
			rows.append({ QStringLiteral("%1").arg(displayNumbers.value(i), digits, 10, QLatin1Char('0')),
				pickerEntries()[i].name, i });
			if (firstVisible < 0)
				firstVisible = i;
			visibleCount++;
		}
	}
	indexList->setRows(rows);

	int target = firstVisible;
	if (jumpMode && !displayOrder.isEmpty())
	{
		// 1-based, clamped: "0" stays on the first line, overshoot stops at
		// the last. The number is the printed page coordinate, fixed at
		// setEntries() time - never the filtered row position.
		target = displayOrder[qBound(0, query.toInt() - 1, displayOrder.size() - 1)];
	}
	indexList->setSelectedEntry(target);
	ensureSelectionVisible();

	countLabel->setText(QStringLiteral("%1/%2").arg(visibleCount).arg(pickerEntries().size()));
}

void MinimalFilterPickerView::moveSelection(int delta)
{
	const QList<MinimalPickerIndexList::Row>& rows = indexList->rows();
	QVector<int> entryOrder;
	entryOrder.reserve(rows.size());
	for (const MinimalPickerIndexList::Row& row : rows)
	{
		if (row.entryIndex >= 0)
			entryOrder.append(row.entryIndex);
	}
	if (entryOrder.isEmpty())
		return;
	int pos = entryOrder.indexOf(indexList->selectedEntry());
	pos = pos < 0 ? 0 : qBound(0, pos + delta, entryOrder.size() - 1);
	indexList->setSelectedEntry(entryOrder[pos]);
	ensureSelectionVisible();
}

void MinimalFilterPickerView::chooseCurrent()
{
	const int index = indexList->selectedEntry();
	if (index >= 0 && indexList->rowOfEntry(index) >= 0)
		emit entryChosen(index);
}

void MinimalFilterPickerView::ensureSelectionVisible()
{
	const int row = indexList->rowOfEntry(indexList->selectedEntry());
	if (row < 0)
		return;
	const QRect r = indexList->rowRect(row);
	// Two calls cover both scroll directions; the margin keeps the adjacent
	// line (or the section caption above) in view as context.
	scrollArea->ensureVisible(0, r.top(), 0, r.height() + 2);
	scrollArea->ensureVisible(0, r.bottom(), 0, r.height() + 2);
}

void MinimalFilterPickerView::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	// The whole control sits in one square hairline frame; no other chrome.
	QPainter painter(this);
	const SkinTokens& t = skinTokens;
	painter.fillRect(rect(), QColor(t.background));
	painter.setPen(QColor(t.border));
	painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

bool MinimalFilterPickerView::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == queryEdit && event->type() == QEvent::KeyPress)
	{
		QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
		switch (keyEvent->key())
		{
		case Qt::Key_Down:
			moveSelection(1);
			return true;
		case Qt::Key_Up:
			moveSelection(-1);
			return true;
		case Qt::Key_PageDown:
			moveSelection(10);
			return true;
		case Qt::Key_PageUp:
			moveSelection(-10);
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			chooseCurrent();
			return true;
		default:
			break;
		}
	}
	return FilterPickerView::eventFilter(watched, event);
}
