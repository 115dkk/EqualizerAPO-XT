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

QFont pickerMonoFont(double pointSize, bool bold = false)
{
	QFont font(SkinManager::instance()->tokens().monoFontFamily);
	font.setPointSizeF(pointSize);
	font.setBold(bold);
	return font;
}
}

// ── MinimalPickerIndexList ───────────────────────────────────────────────────

MinimalPickerIndexList::MinimalPickerIndexList(QWidget* parent)
	: QWidget(parent)
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
	const SkinTokens& t = SkinManager::instance()->tokens();
	painter.fillRect(rect(), QColor(t.background));

	QFont entryFont = pickerMonoFont(9.0);
	QFont captionFont = pickerMonoFont(7.5, true);
	captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
	const QFontMetrics entryMetrics(entryFont);

	const int pad = sidePadding();
	const int numberGap = GUIHelper::scale(8.0);

	for (int i = 0; i < rowList.size(); i++)
	{
		const Row& row = rowList[i];
		const QRect r = rowRect(i);
		if (!r.intersects(event->rect()))
			continue;

		if (row.entryIndex < 0)
		{
			// Section caption: uppercase letter-spaced mono over a full-width
			// hairline rule. The rule is the only "decoration" the index has,
			// and it is really a separator, i.e. information.
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
				// Inverted block: the line trades foreground for background,
				// the bluntest possible cursor a text instrument can have.
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

// ── MinimalFilterPickerView ──────────────────────────────────────────────────

MinimalFilterPickerView::MinimalFilterPickerView(QWidget* parent)
	: FilterPickerView(parent)
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
	connect(queryEdit, &QLineEdit::textChanged, this, &MinimalFilterPickerView::rebuildIndex);
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
	indexList = new MinimalPickerIndexList(scrollArea);
	indexList->onEntryActivated = [this](int entryIndex)
	{
		emit entryChosen(entryIndex);
	};
	scrollArea->setWidget(indexList);
	layout->addWidget(scrollArea, 1);

	// Key legend, the BIOS-menu signature. Plain text, hairline above (QSS);
	// the arrows are built from code points so the source stays pure ASCII.
	const QString dot = QStringLiteral(" %1 ").arg(QChar(0x00B7));
	QLabel* legend = new QLabel(
		QString(QChar(0x2191)) + QChar(0x2193) + QStringLiteral(" MOVE") + dot
		+ QStringLiteral("NN JUMP") + dot + QStringLiteral("RET INSERT"), this);
	legend->setObjectName(QStringLiteral("MinimalPickerLegend"));
	layout->addWidget(legend);

	// The host calls view->setFocus(); typing must land in the query line.
	setFocusProxy(queryEdit);
	setMinimumWidth(GUIHelper::scale(360.0));
	setMaximumHeight(GUIHelper::scale(460.0));
}

void MinimalFilterPickerView::setEntries(const QList<FilterPickerEntry>& entries)
{
	allEntries = entries;
	rebuildIndex();
	queryEdit->setFocus();
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
	const QString query = queryEdit->text().trimmed();

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
	const QStringList terms = jumpMode
		? QStringList()
		: query.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

	// Coalesce by path (first-appearance order): one caption per category,
	// each entry keeping its stable original number. Factories may revisit a
	// category, and a repeated caption would read as corruption in an index.
	QStringList sectionOrder;
	QHash<QString, QList<int>> sectionEntries;
	for (int i = 0; i < allEntries.size(); i++)
	{
		const FilterPickerEntry& entry = allEntries[i];
		const QString section = entry.path.isEmpty()
			? tr("General") : entry.path.join(QStringLiteral(" / "));

		bool matches = true;
		const QString haystack = section + QLatin1Char(' ') + entry.name + QLatin1Char(' ') + entry.line;
		for (const QString& term : terms)
		{
			if (!haystack.contains(term, Qt::CaseInsensitive))
			{
				matches = false;
				break;
			}
		}
		if (!matches)
			continue;

		if (!sectionEntries.contains(section))
			sectionOrder.append(section);
		sectionEntries[section].append(i);
	}

	const int digits = qMax(2, QString::number(allEntries.size()).size());
	QList<MinimalPickerIndexList::Row> rows;
	int firstVisible = -1;
	int visibleCount = 0;
	for (const QString& section : sectionOrder)
	{
		rows.append({ QString(), section.toUpper(), -1 });
		for (int i : sectionEntries.value(section))
		{
			rows.append({ QStringLiteral("%1").arg(i + 1, digits, 10, QLatin1Char('0')), allEntries[i].name, i });
			if (firstVisible < 0)
				firstVisible = i;
			visibleCount++;
		}
	}
	indexList->setRows(rows);

	int target = firstVisible;
	if (jumpMode && !allEntries.isEmpty())
	{
		// 1-based, clamped: "0" stays on the first entry, overshoot stops at
		// the last. The index is original and stable, never the filtered row.
		target = qBound(0, query.toInt() - 1, allEntries.size() - 1);
	}
	indexList->setSelectedEntry(target);
	ensureSelectionVisible();

	countLabel->setText(QStringLiteral("%1/%2").arg(visibleCount).arg(allEntries.size()));
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
	const SkinTokens& t = SkinManager::instance()->tokens();
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
