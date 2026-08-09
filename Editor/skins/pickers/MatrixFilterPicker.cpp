/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixFilterPicker.h"
#include "Editor/skins/SkinPaint.h"

#include <QFontMetrics>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

namespace
{
// Unscaled metric constants; everything is multiplied by GUIHelper::scale so
// the board keeps its 1px-rule density on high-DPI screens.
namespace Metrics
{
constexpr double headerHeight = 32.0;
constexpr double footerHeight = 22.0;
constexpr double cellHeight = 30.0;
// The faint column grid behind the entry cells; same pitch as the card
// chrome's graph paper (MatrixMetrics::gridPitch).
constexpr double gridPitch = 24.0;
constexpr double railMinWidth = 132.0;
constexpr double railMaxWidth = 204.0;
constexpr double entryMinWidth = 240.0;
// Keeps the whole instrument inside the host's ~300-460px width window.
constexpr double panelMaxWidth = 458.0;
constexpr double panelMaxHeight = 478.0;
}

int s(double pixel)
{
	return GUIHelper::scale(pixel);
}
}

MatrixFilterPickerView::MatrixFilterPickerView(QWidget* parent)
	: FilterPickerView(parent)
{
	setObjectName(QStringLiteral("MatrixFilterPicker"));
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
}

void MatrixFilterPickerView::entriesChanged()
{
	query.clear();
	rebuildBuses(pickerEntries());
	applyQuery();
	computeMetrics();
	setFixedSize(computedSize);
	updateGeometry();
	update();
}

QSize MatrixFilterPickerView::sizeHint() const
{
	return computedSize.isValid() ? computedSize : QSize(s(360.0), s(220.0));
}

void MatrixFilterPickerView::galleryShowcase(GalleryShowcase kind)
{
	if (kind == GalleryShowcase::PhaseAndTimeSearch)
	{
		// Where Delay and the two all-pass sections went. The category is part
		// of what the shared predicate searches, so one term returns the whole
		// group.
		query = QStringLiteral("phase");
		applyQuery();
		update();
		return;
	}

	if (kind == GalleryShowcase::EmptySearch)
	{
		// A scan no template answers: every bus count drops to 0, the rail
		// dims, and the entry column posts the NO SIGNAL board notation
		// painted by paintEvent.
		query = QStringLiteral("zzzz");
		applyQuery();
		update();
		return;
	}

	// HoverFirstEntry: stage the crosspoint pre-light. The cursor already
	// engages the first cell when the board opens, and an engaged cell shows
	// no hover band, so the staged hover sits on the first cell the cursor
	// is not holding - plus the next answering bus, so both pre-light
	// treatments (entry cell, bus cell) are in the same shot.
	query.clear();
	applyQuery();
	hoverRow = visibleRows(activeBus).size() > 1 ? 1 : 0;
	hoverBus = -1;
	for (int b = 0; b < buses.size(); b++)
	{
		if (b != activeBus && busHasMatches(b))
		{
			hoverBus = b;
			break;
		}
	}
	update();
}

void MatrixFilterPickerView::rebuildBuses(const QList<FilterPickerEntry>& entries)
{
	buses.clear();

	QHash<QString, int> busOf;
	int generalBus = -1;
	for (int i = 0; i < entries.size(); i++)
	{
		const FilterPickerEntry& entry = entries[i];
		const bool general = entry.path.isEmpty();
		const QString key = general ? QString() : entry.path.join(QStringLiteral(" / "));
		int busIndex = busOf.value(key, -1);
		if (busIndex < 0)
		{
			Bus bus;
			bus.label = (general ? tr("General") : key).toUpper();
			buses.append(bus);
			busIndex = buses.size() - 1;
			busOf.insert(key, busIndex);
			if (general)
				generalBus = busIndex;
		}
		Cell cell;
		cell.entryIndex = i;
		cell.name = entry.name;
		cell.line = entry.line;
		buses[busIndex].cells.append(cell);
	}

	// The catch-all bus parks at the bottom of the rail; real categories keep
	// their first-appearance order.
	if (generalBus >= 0 && generalBus != buses.size() - 1)
		buses.append(buses.takeAt(generalBus));

	// Stable board coordinates: bus letter + 1-based cell number. They never
	// renumber while scanning.
	for (int b = 0; b < buses.size(); b++)
	{
		buses[b].letter = QString(QChar('A' + (b % 26)));
		for (int c = 0; c < buses[b].cells.size(); c++)
			buses[b].cells[c].coordinate = buses[b].letter + QString::number(c + 1);
	}

	// The board opens on the densest bus (the parametric filters in practice)
	// so the instrument never opens onto a near-empty column.
	activeBus = -1;
	int bestCount = -1;
	for (int b = 0; b < buses.size(); b++)
	{
		if (buses[b].cells.size() > bestCount)
		{
			bestCount = buses[b].cells.size();
			activeBus = b;
		}
	}
	cursorRow = buses.isEmpty() ? -1 : 0;
	hoverBus = -1;
	hoverRow = -1;
}

void MatrixFilterPickerView::computeMetrics()
{
	headerH = s(Metrics::headerHeight);
	footerH = s(Metrics::footerHeight);
	cellH = s(Metrics::cellHeight);

	const QFontMetrics busMetrics(monoFont(7.5, true, 0.5));
	const QFontMetrics nameMetrics(monoFont(8.5, false));
	const QFontMetrics coordMetrics(monoFont(7.0, true));

	int maxLabel = 0;
	int maxName = 0;
	int maxCells = 0;
	QString widestCoordinate = QStringLiteral("A1");
	for (const Bus& bus : buses)
	{
		maxLabel = qMax(maxLabel, busMetrics.horizontalAdvance(bus.label));
		maxCells = qMax(maxCells, bus.cells.size());
		for (const Cell& cell : bus.cells)
		{
			maxName = qMax(maxName, nameMetrics.horizontalAdvance(cell.name));
			if (cell.coordinate.size() > widestCoordinate.size())
				widestCoordinate = cell.coordinate;
		}
	}
	coordW = qMax(s(22.0), coordMetrics.horizontalAdvance(widestCoordinate) + s(8.0));

	const int countW = coordMetrics.horizontalAdvance(QStringLiteral("88"));
	railW = qBound(s(Metrics::railMinWidth),
			s(8.0) + s(16.0) + s(6.0) + maxLabel + s(6.0) + countW + s(8.0),
			s(Metrics::railMaxWidth));
	entryW = qBound(s(Metrics::entryMinWidth),
			s(8.0) + coordW + s(8.0) + maxName + s(10.0),
			qMax(s(Metrics::entryMinWidth), s(Metrics::panelMaxWidth) - railW - 3));

	// Both columns share one fixed body height so switching buses never
	// resizes the popup; the grid simply has dark cells where a bus is short.
	bodyRows = qMax(buses.size(), maxCells);
	if (bodyRows < 1)
		bodyRows = 1;
	if (headerH + bodyRows * cellH + footerH + 2 > s(Metrics::panelMaxHeight))
		cellH = qMax(s(20.0), (s(Metrics::panelMaxHeight) - headerH - footerH - 2) / bodyRows);

	computedSize = QSize(railW + entryW + 3, headerH + bodyRows * cellH + footerH + 2);
}

void MatrixFilterPickerView::applyQuery()
{
	for (Bus& bus : buses)
	{
		for (Cell& cell : bus.cells)
		{
			const FilterPickerEntry entry{ {}, cell.name, cell.line, {} };
			cell.matches = filterPickerMatches(entry, bus.label, query);
		}
	}

	// Keep the operator's bus when it still answers; otherwise jump to the
	// first bus that does.
	if (activeBus < 0 || !busHasMatches(activeBus))
		activeBus = firstBusWithMatches();
	const int visibleCount = activeBus >= 0 ? visibleRows(activeBus).size() : 0;
	if (visibleCount == 0)
		cursorRow = -1;
	else
		cursorRow = qBound(0, cursorRow, visibleCount - 1);
}

QVector<int> MatrixFilterPickerView::visibleRows(int busIndex) const
{
	QVector<int> rows;
	if (busIndex < 0 || busIndex >= buses.size())
		return rows;
	for (int c = 0; c < buses[busIndex].cells.size(); c++)
	{
		if (buses[busIndex].cells[c].matches)
			rows.append(c);
	}
	return rows;
}

bool MatrixFilterPickerView::busHasMatches(int busIndex) const
{
	if (busIndex < 0 || busIndex >= buses.size())
		return false;
	for (const Cell& cell : buses[busIndex].cells)
	{
		if (cell.matches)
			return true;
	}
	return false;
}

int MatrixFilterPickerView::firstBusWithMatches() const
{
	for (int b = 0; b < buses.size(); b++)
	{
		if (busHasMatches(b))
			return b;
	}
	return -1;
}

const MatrixFilterPickerView::Cell* MatrixFilterPickerView::cursorCell() const
{
	if (activeBus < 0 || cursorRow < 0)
		return nullptr;
	const QVector<int> rows = visibleRows(activeBus);
	if (cursorRow >= rows.size())
		return nullptr;
	return &buses[activeBus].cells[rows[cursorRow]];
}

void MatrixFilterPickerView::chooseCursor()
{
	const Cell* cell = cursorCell();
	if (cell != nullptr)
		emit entryChosen(cell->entryIndex);
}

void MatrixFilterPickerView::moveCursor(int delta)
{
	if (buses.isEmpty())
		return;
	if (activeBus < 0)
	{
		activeBus = firstBusWithMatches();
		cursorRow = activeBus >= 0 ? 0 : -1;
		update();
		return;
	}
	const int count = visibleRows(activeBus).size();
	const int next = cursorRow + delta;
	if (next >= 0 && next < count)
	{
		cursorRow = next;
		update();
		return;
	}
	// Walking off either end of a bus continues onto the adjacent bus, so
	// Up/Down alone traverses the whole catalog.
	int b = activeBus;
	for (int step = 0; step < buses.size(); step++)
	{
		b = (b + (delta > 0 ? 1 : -1) + buses.size()) % buses.size();
		const int n = visibleRows(b).size();
		if (n > 0)
		{
			activeBus = b;
			cursorRow = delta > 0 ? 0 : n - 1;
			update();
			return;
		}
	}
}

void MatrixFilterPickerView::switchBus(int delta)
{
	if (buses.isEmpty())
		return;
	int b = activeBus < 0 ? firstBusWithMatches() : activeBus;
	if (b < 0)
		return;
	for (int step = 0; step < buses.size(); step++)
	{
		b = (b + (delta > 0 ? 1 : -1) + buses.size()) % buses.size();
		if (busHasMatches(b))
		{
			activeBus = b;
			cursorRow = 0;
			update();
			return;
		}
	}
}

QRect MatrixFilterPickerView::headerRect() const
{
	return QRect(1, 1, width() - 2, headerH);
}

QRect MatrixFilterPickerView::bodyRect() const
{
	return QRect(1, 1 + headerH, width() - 2, height() - 2 - headerH - footerH);
}

QRect MatrixFilterPickerView::railRect() const
{
	const QRect body = bodyRect();
	return QRect(body.left(), body.top(), railW, body.height());
}

QRect MatrixFilterPickerView::entriesRect() const
{
	const QRect body = bodyRect();
	return QRect(body.left() + railW + 1, body.top(), body.width() - railW - 1, body.height());
}

QRect MatrixFilterPickerView::footerRect() const
{
	return QRect(1, height() - 1 - footerH, width() - 2, footerH);
}

QRect MatrixFilterPickerView::busCellRect(int busIndex) const
{
	const QRect rail = railRect();
	return QRect(rail.left(), rail.top() + busIndex * cellH, rail.width(), cellH);
}

QRect MatrixFilterPickerView::entryCellRect(int visibleRow) const
{
	const QRect entries = entriesRect();
	return QRect(entries.left(), entries.top() + visibleRow * cellH, entries.width(), cellH);
}

QFont MatrixFilterPickerView::monoFont(double pointSize, bool bold, double letterSpacing) const
{
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	QFont font(tokens.monoFontFamily);
	font.setFamilies(QStringList()
		<< tokens.monoFontFamily
		<< QStringLiteral("Consolas")
		<< QStringLiteral("Malgun Gothic"));
	font.setPointSizeF(pointSize);
	font.setBold(bold);
	if (letterSpacing > 0.0)
		font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
	return font;
}

void MatrixFilterPickerView::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QColor border(tokens.border);
	const QColor accent(tokens.accent);
	const QColor textColor(tokens.text);
	const QColor muted(tokens.mutedText);
	const QColor dimmed = withAlpha(muted, 110);
	const QColor sunken(tokens.surfaceSunken);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, false);

	painter.fillRect(rect(), QColor(tokens.surface));

	// ---- Header: panel designation + scan readout strip ----
	const QRect header = headerRect();
	painter.fillRect(header, QColor(tokens.cardHover));

	const int pad = s(10.0);
	const QRect lamp(header.left() + pad, header.center().y() - s(2.0), s(5.0), s(5.0));
	painter.fillRect(lamp, accent);
	painter.setFont(monoFont(7.5, true, 2.0));
	painter.setPen(muted);
	painter.drawText(QRect(lamp.right() + s(8.0), header.top(), header.width() / 2, header.height()),
		Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("INSERT"));

	// Scan readout: a sunken status cell, not a search box. Typing anywhere
	// in the panel writes into it.
	const int boxW = qMin(s(160.0), header.width() / 2);
	const QRect box(header.right() - pad - boxW,
		header.top() + (header.height() - s(18.0)) / 2, boxW, s(18.0));
	painter.fillRect(box, sunken);
	painter.setPen(QPen(query.isEmpty() ? border : accent, 1));
	painter.drawRect(box.adjusted(0, 0, -1, -1));
	const QFont scanFont = monoFont(8.0, false);
	const QFontMetrics scanMetrics(scanFont);
	painter.setFont(scanFont);
	int textX = box.left() + s(6.0);
	painter.setPen(accent);
	painter.drawText(QRect(textX, box.top(), box.width(), box.height()),
		Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral(">"));
	textX += scanMetrics.horizontalAdvance(QStringLiteral("> "));
	const int caretWidth = s(6.0);
	const int queryAvail = box.right() - s(6.0) - caretWidth - textX;
	const QString shownQuery = scanMetrics.elidedText(query, Qt::ElideLeft, qMax(0, queryAvail));
	painter.setPen(textColor);
	painter.drawText(QRect(textX, box.top(), qMax(0, queryAvail), box.height()),
		Qt::AlignVCenter | Qt::AlignLeft, shownQuery);
	textX += scanMetrics.horizontalAdvance(shownQuery) + 1;
	painter.fillRect(QRect(textX, box.top() + s(4.0), caretWidth, box.height() - s(8.0)), accent);
	if (query.isEmpty())
	{
		painter.setPen(dimmed);
		painter.drawText(box.adjusted(0, 0, -s(6.0), 0),
			Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("SCAN"));
	}

	painter.setPen(QPen(border, 1));
	painter.drawLine(header.left(), header.bottom(), header.right(), header.bottom());

	// ---- Bus rail (left column of category selects) ----
	const QRect rail = railRect();
	const QFont busFont = monoFont(7.5, true, 0.5);
	const QFont coordFont = monoFont(7.0, true);
	const QFontMetrics coordMetrics(coordFont);
	const int countW = coordMetrics.horizontalAdvance(QStringLiteral("88"));
	for (int b = 0; b < buses.size(); b++)
	{
		const QRect cell = busCellRect(b);
		const bool active = b == activeBus;
		const bool answering = busHasMatches(b);
		const bool hovered = b == hoverBus && answering && !active;

		if (active)
		{
			painter.fillRect(cell, withAlpha(accent, 48));
			painter.fillRect(QRect(cell.left(), cell.top(), s(3.0), cell.height()), accent);
		}
		else if (hovered)
		{
			// Pre-light alpha: values around 16 measured ~3.5% brightness delta
			// on the rendered board - below the perceptual threshold, so hover
			// appeared to highlight nothing. Engagement stays clearly above it
			// (fill + 3px band).
			painter.fillRect(cell, withAlpha(accent, 40));
		}
		painter.setPen(QPen(border, 1));
		painter.drawLine(cell.left(), cell.bottom(), cell.right(), cell.bottom());

		// Bus designation cell.
		const QRect letterBox(cell.left() + s(8.0), cell.center().y() - s(8.0), s(16.0), s(16.0));
		painter.fillRect(letterBox, sunken);
		painter.setPen(QPen(active ? accent : border, 1));
		painter.drawRect(letterBox.adjusted(0, 0, -1, -1));
		painter.setFont(coordFont);
		painter.setPen(!answering ? dimmed : (active ? accent : muted));
		painter.drawText(letterBox, Qt::AlignCenter, buses[b].letter);

		// Channel count: how many cells of this bus answer the scan.
		const int visibleCount = visibleRows(b).size();
		painter.setPen(!answering ? dimmed : muted);
		painter.drawText(QRect(cell.right() - s(8.0) - countW, cell.top(), countW, cell.height()),
			Qt::AlignVCenter | Qt::AlignRight, QString::number(visibleCount));

		// Bus caption.
		painter.setFont(busFont);
		const QFontMetrics busMetrics(busFont);
		const int labelX = letterBox.right() + s(6.0);
		const int labelAvail = cell.right() - s(8.0) - countW - s(4.0) - labelX;
		painter.setPen(!answering ? dimmed : (active ? textColor : muted));
		painter.drawText(QRect(labelX, cell.top(), qMax(0, labelAvail), cell.height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			busMetrics.elidedText(buses[b].label, Qt::ElideRight, qMax(0, labelAvail)));
	}

	// ---- Entry column (the active bus's cells) ----
	const QRect entries = entriesRect();
	painter.fillRect(entries, QColor(tokens.card));

	// Faint column grid: the graph paper the cells sit on.
	painter.setPen(QPen(withAlpha(border, 50), 1));
	for (int x = entries.left() + s(Metrics::gridPitch); x < entries.right(); x += s(Metrics::gridPitch))
		painter.drawLine(x, entries.top(), x, entries.bottom());

	const QVector<int> rows = visibleRows(activeBus);
	const QFont nameFont = monoFont(8.5, false);
	const QFontMetrics nameMetrics(nameFont);
	for (int r = 0; r < rows.size(); r++)
	{
		const Cell& cell = buses[activeBus].cells[rows[r]];
		const QRect cellRect = entryCellRect(r);
		const bool engaged = r == cursorRow;
		const bool hovered = r == hoverRow && !engaged;

		if (engaged)
			painter.fillRect(cellRect, withAlpha(accent, 56));
		else if (hovered)
			// Pre-light alpha (see the bus-rail note): below the engaged fill
			// (56 + accent rule + patch trace) so pre-light never reads as
			// engagement.
			painter.fillRect(cellRect, withAlpha(accent, 40));
		painter.setPen(QPen(border, 1));
		painter.drawLine(cellRect.left(), cellRect.bottom(), cellRect.right(), cellRect.bottom());
		if (engaged)
		{
			painter.setPen(QPen(accent, 1));
			painter.drawRect(cellRect.adjusted(0, 0, -1, -1));
		}

		// Coordinate cell.
		const QRect coordBox(cellRect.left() + s(8.0), cellRect.center().y() - s(8.0), coordW, s(16.0));
		painter.fillRect(coordBox, sunken);
		painter.setPen(QPen(engaged ? accent : border, 1));
		painter.drawRect(coordBox.adjusted(0, 0, -1, -1));
		painter.setFont(coordFont);
		painter.setPen(engaged ? accent : muted);
		painter.drawText(coordBox, Qt::AlignCenter, cell.coordinate);

		// Template name in board mono.
		painter.setFont(nameFont);
		painter.setPen(textColor);
		const int nameX = coordBox.right() + s(8.0);
		const int nameAvail = cellRect.right() - s(8.0) - nameX;
		painter.drawText(QRect(nameX, cellRect.top(), qMax(0, nameAvail), cellRect.height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			nameMetrics.elidedText(cell.name, Qt::ElideRight, qMax(0, nameAvail)));
	}

	// A fruitless scan never blanks the board: the entry column posts the
	// NO SIGNAL notation where the cells would be.
	if (rows.isEmpty())
	{
		painter.setFont(monoFont(8.5, true, 2.0));
		painter.setPen(dimmed);
		painter.drawText(entries, Qt::AlignCenter, QStringLiteral("-- NO SIGNAL --"));
	}

	// Divider rule between rail and entry column.
	const int dividerX = rail.right() + 1;
	painter.setPen(QPen(border, 1));
	painter.drawLine(dividerX, bodyRect().top(), dividerX, bodyRect().bottom());

	// Patch trace: the engaged crosspoint is wired from its bus cell to the
	// cursor cell across the divider.
	if (activeBus >= 0 && cursorRow >= 0 && cursorRow < rows.size())
	{
		const int busY = busCellRect(activeBus).center().y();
		const int cursorY = entryCellRect(cursorRow).center().y();
		painter.fillRect(QRect(dividerX - s(6.0), busY, s(6.0), 2), accent);
		painter.fillRect(QRect(dividerX, qMin(busY, cursorY), 2, qAbs(cursorY - busY) + 2), accent);
		painter.fillRect(QRect(dividerX + 2, cursorY, s(6.0), 2), accent);
	}

	// ---- Footer: readout of the line the engaged coordinate inserts ----
	const QRect footer = footerRect();
	painter.fillRect(footer, sunken);
	painter.setPen(QPen(border, 1));
	painter.drawLine(footer.left(), footer.top(), footer.right(), footer.top());
	const QFont footFont = monoFont(7.5, false);
	const QFontMetrics footMetrics(footFont);
	painter.setFont(footFont);
	const Cell* engagedCell = cursorCell();
	const QString coordText = engagedCell != nullptr ? engagedCell->coordinate : QStringLiteral("--");
	const int coordTextW = footMetrics.horizontalAdvance(coordText);
	painter.setPen(engagedCell != nullptr ? accent : dimmed);
	painter.drawText(QRect(footer.right() - pad - coordTextW, footer.top(), coordTextW, footer.height()),
		Qt::AlignVCenter | Qt::AlignRight, coordText);
	const QString lineText = engagedCell != nullptr
		? QStringLiteral("> ") + engagedCell->line
		: QStringLiteral("> NO MATCH");
	const int lineAvail = footer.width() - 2 * pad - coordTextW - s(12.0);
	painter.setPen(muted);
	painter.drawText(QRect(footer.left() + pad, footer.top(), qMax(0, lineAvail), footer.height()),
		Qt::AlignVCenter | Qt::AlignLeft,
		footMetrics.elidedText(lineText, Qt::ElideRight, qMax(0, lineAvail)));

	// Outer 1px rule, square corners.
	painter.setPen(QPen(border, 1));
	painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void MatrixFilterPickerView::keyPressEvent(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_Down:
		moveCursor(1);
		return;
	case Qt::Key_Up:
		moveCursor(-1);
		return;
	case Qt::Key_Right:
	case Qt::Key_PageDown:
		switchBus(1);
		return;
	case Qt::Key_Left:
	case Qt::Key_PageUp:
		switchBus(-1);
		return;
	case Qt::Key_Home:
		if (!visibleRows(activeBus).isEmpty())
		{
			cursorRow = 0;
			update();
		}
		return;
	case Qt::Key_End:
	{
		const int count = visibleRows(activeBus).size();
		if (count > 0)
		{
			cursorRow = count - 1;
			update();
		}
		return;
	}
	case Qt::Key_Return:
	case Qt::Key_Enter:
		chooseCursor();
		return;
	case Qt::Key_Backspace:
		if (!query.isEmpty())
		{
			query.chop(1);
			applyQuery();
			update();
		}
		return;
	default:
		break;
	}

	// Printable input writes into the scan readout. Esc is deliberately not
	// handled: the popup host owns dismissal.
	const QString input = event->text();
	if (!input.isEmpty() && input.at(0).isPrint()
		&& !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
	{
		query += input;
		applyQuery();
		update();
		return;
	}
	FilterPickerView::keyPressEvent(event);
}

void MatrixFilterPickerView::mouseMoveEvent(QMouseEvent* event)
{
	const QPoint pos = event->pos();
	int newHoverBus = -1;
	int newHoverRow = -1;
	if (railRect().contains(pos))
	{
		const int b = (pos.y() - railRect().top()) / qMax(1, cellH);
		if (b >= 0 && b < buses.size())
			newHoverBus = b;
	}
	else if (entriesRect().contains(pos))
	{
		const int r = (pos.y() - entriesRect().top()) / qMax(1, cellH);
		if (r >= 0 && r < visibleRows(activeBus).size())
			newHoverRow = r;
	}
	if (newHoverBus != hoverBus || newHoverRow != hoverRow)
	{
		hoverBus = newHoverBus;
		hoverRow = newHoverRow;
		update();
	}
	FilterPickerView::mouseMoveEvent(event);
}

void MatrixFilterPickerView::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		const QPoint pos = event->pos();
		if (railRect().contains(pos))
		{
			const int b = (pos.y() - railRect().top()) / qMax(1, cellH);
			if (b >= 0 && b < buses.size() && busHasMatches(b) && b != activeBus)
			{
				activeBus = b;
				cursorRow = 0;
				update();
			}
			return;
		}
		if (entriesRect().contains(pos))
		{
			const int r = (pos.y() - entriesRect().top()) / qMax(1, cellH);
			const QVector<int> rows = visibleRows(activeBus);
			if (r >= 0 && r < rows.size())
			{
				// Dropdown semantics: engaging a crosspoint inserts.
				cursorRow = r;
				emit entryChosen(buses[activeBus].cells[rows[r]].entryIndex);
			}
			return;
		}
	}
	FilterPickerView::mousePressEvent(event);
}

void MatrixFilterPickerView::leaveEvent(QEvent* event)
{
	if (hoverBus != -1 || hoverRow != -1)
	{
		hoverBus = -1;
		hoverRow = -1;
		update();
	}
	FilterPickerView::leaveEvent(event);
}
