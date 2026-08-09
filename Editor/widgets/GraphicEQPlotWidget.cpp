#include "GraphicEQPlotWidget.h"

#include <algorithm>
#include <cmath>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

using std::vector;

namespace
{
// The audible window the frequency axis is pinned to.
const double minHz = 20.0;
const double maxHz = 20000.0;
// dB frame limits: tight enough to stay a usable editor, wide enough for
// every serious correction curve.
const double minSpanDb = 12.0;
const double maxSpanDb = 120.0;
const double dbCeiling = 100.0;

// Grid label for a frequency ("20", "1k", "20k").
QString hzLabel(double hz)
{
	if (hz >= 1000.0)
	{
		const double k = hz / 1000.0;
		return qFuzzyCompare(k, double(qRound(k))) ? QStringLiteral("%1k").arg(qRound(k)) : QStringLiteral("%1k").arg(k);
	}
	return QString::number(qRound(hz));
}

QString dbLabel(double db)
{
	const int rounded = qRound(db);
	return rounded > 0 ? QStringLiteral("+%1").arg(rounded) : QString::number(rounded);
}
}

GraphicEQPlotWidget::GraphicEQPlotWidget(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("GraphicEQPlot"));
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		update();
	});
}

void GraphicEQPlotWidget::setNodes(const vector<FilterNode>& value)
{
	nodeList = value;
	std::sort(nodeList.begin(), nodeList.end());
	selection.clear();
	hovered = -1;
	dragged = -1;
	setFocusedNode(nodeList.empty() ? -1 : 0);
	update();
}

const vector<FilterNode>& GraphicEQPlotWidget::nodes() const
{
	return nodeList;
}

void GraphicEQPlotWidget::setBandCount(int value)
{
	if (bands == value)
		return;
	bands = value;
	update();
}

int GraphicEQPlotWidget::bandCount() const
{
	return bands;
}

int GraphicEQPlotWidget::focusedNode() const
{
	return focused;
}

const QSet<int>& GraphicEQPlotWidget::selectedNodes() const
{
	return selection;
}

void GraphicEQPlotWidget::setNodeValues(int index, double hz, double db)
{
	if (index < 0 || index >= int(nodeList.size()))
		return;
	const int newIndex = moveNode(index, hz, db);
	if (focused == index && newIndex != index)
		setFocusedNode(newIndex);
	update();
	emit nodesEdited();
}

void GraphicEQPlotWidget::setFrame(double newTopDb, double newSpanDb)
{
	spanDb = qBound(minSpanDb, newSpanDb, maxSpanDb);
	topDb = qBound(-dbCeiling + spanDb, newTopDb, dbCeiling);
	update();
}

double GraphicEQPlotWidget::frameTopDb() const
{
	return topDb;
}

double GraphicEQPlotWidget::frameSpanDb() const
{
	return spanDb;
}

void GraphicEQPlotWidget::frameToResponse()
{
	double minGain = 0.0;
	double maxGain = 0.0;
	for (const FilterNode& node : nodeList)
	{
		minGain = qMin(minGain, node.dbGain);
		maxGain = qMax(maxGain, node.dbGain);
	}
	const double span = qBound(minSpanDb, qMax(42.0, maxGain - minGain + 12.0), maxSpanDb);
	setFrame((minGain + maxGain) / 2.0 + span / 2.0, span);
}

QSize GraphicEQPlotWidget::sizeHint() const
{
	return QSize(400, GUIHelper::scale(210));
}

QRectF GraphicEQPlotWidget::plotRect() const
{
	// Label margins: dB column on the left, Hz line at the bottom. Skins
	// receive these areas through state.rect vs state.plotRect.
	return QRectF(rect()).adjusted(GUIHelper::scale(34), GUIHelper::scale(6), -GUIHelper::scale(8), -GUIHelper::scale(18));
}

double GraphicEQPlotWidget::hzToX(double hz) const
{
	const QRectF area = plotRect();
	const double t = std::log(qBound(minHz, hz, maxHz) / minHz) / std::log(maxHz / minHz);
	return area.left() + t * area.width();
}

double GraphicEQPlotWidget::xToHz(double x) const
{
	const QRectF area = plotRect();
	const double t = qBound(0.0, (x - area.left()) / area.width(), 1.0);
	return minHz * std::pow(maxHz / minHz, t);
}

double GraphicEQPlotWidget::dbToY(double db) const
{
	const QRectF area = plotRect();
	return area.top() + (topDb - db) / spanDb * area.height();
}

double GraphicEQPlotWidget::yToDb(double y) const
{
	const QRectF area = plotRect();
	return topDb - (y - area.top()) / area.height() * spanDb;
}

int GraphicEQPlotWidget::nodeAt(const QPointF& pos) const
{
	// 10px grab radius: generous for a precision surface, and every node
	// stays reachable through the readout strip and the keyboard as well.
	const double radius = GUIHelper::scale(10.0);
	int best = -1;
	double bestDistance = radius * radius;
	for (int i = 0; i < int(nodeList.size()); i++)
	{
		const QPointF center(hzToX(nodeList[i].freq), dbToY(nodeList[i].dbGain));
		const QPointF delta = center - pos;
		const double distance = delta.x() * delta.x() + delta.y() * delta.y();
		if (distance <= bestDistance)
		{
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

void GraphicEQPlotWidget::setFocusedNode(int index)
{
	if (focused == index)
		return;
	focused = index;
	emit focusedNodeChanged(focused);
}

int GraphicEQPlotWidget::moveNode(int index, double hz, double db)
{
	FilterNode node = nodeList[size_t(index)];
	node.freq = bands == -1 ? qBound(minHz, hz, maxHz) : node.freq;
	node.dbGain = qBound(-dbCeiling, db, dbCeiling);

	// Keep the vector sorted by bubbling the node across neighbours, exactly
	// like the legacy scene's itemMoved, so serialization order never breaks.
	int newIndex = index;
	nodeList[size_t(index)] = node;
	while (newIndex > 0 && nodeList[size_t(newIndex - 1)].freq > node.freq)
	{
		std::swap(nodeList[size_t(newIndex - 1)], nodeList[size_t(newIndex)]);
		newIndex--;
	}
	while (newIndex + 1 < int(nodeList.size()) && nodeList[size_t(newIndex + 1)].freq < node.freq)
	{
		std::swap(nodeList[size_t(newIndex)], nodeList[size_t(newIndex + 1)]);
		newIndex++;
	}

	if (newIndex != index)
	{
		// Selection indices shift with the reorder; rebuild the set.
		QSet<int> moved;
		for (int selected : selection)
		{
			if (selected == index)
				moved.insert(newIndex);
			else if (index < newIndex && selected > index && selected <= newIndex)
				moved.insert(selected - 1);
			else if (newIndex < index && selected >= newIndex && selected < index)
				moved.insert(selected + 1);
			else
				moved.insert(selected);
		}
		selection = moved;
		if (hovered == index)
			hovered = newIndex;
	}
	return newIndex;
}

void GraphicEQPlotWidget::addNodeAt(const QPointF& pos)
{
	double hz = xToHz(pos.x());
	hz = hz >= 10000.0 ? std::round(hz) : std::round(hz * 10.0) / 10.0;
	double db = yToDb(pos.y());
	db = std::round(db * 10.0) / 10.0;

	FilterNode node(hz, db);
	const auto it = std::lower_bound(nodeList.begin(), nodeList.end(), node);
	const int index = int(it - nodeList.begin());
	nodeList.insert(it, node);
	selection = { index };
	setFocusedNode(index);
	update();
	emit nodesEdited();
}

void GraphicEQPlotWidget::removeSelected()
{
	if (selection.isEmpty())
		return;
	QList<int> ordered = selection.values();
	std::sort(ordered.begin(), ordered.end(), std::greater<int>());
	for (int index : ordered)
		nodeList.erase(nodeList.begin() + index);
	selection.clear();
	hovered = -1;
	setFocusedNode(nodeList.empty() ? -1 : qMin(ordered.last(), int(nodeList.size()) - 1));
	if (focused >= 0)
		selection = { focused };
	update();
	emit nodesEdited();
}

GraphicEQPlotState GraphicEQPlotWidget::buildState() const
{
	GraphicEQPlotState state;
	state.rect = rect();
	state.plotRect = plotRect();
	state.enabled = isEnabled();
	state.focused = hasFocus();
	state.bandLocked = bands != -1;
	state.selectedNodes = selection;
	state.hoveredNode = hovered;
	state.focusedNode = focused;
	state.zeroY = dbToY(0.0);

	// Response curve sampled every 2px through the shared engine-side
	// interpolator, so the drawn response and the audible one agree.
	GainCurveIterator gainIterator(const_cast<vector<FilterNode>&>(nodeList));
	const int step = 2;
	for (int x = int(state.plotRect.left()); x <= int(state.plotRect.right()); x += step)
	{
		const double db = gainIterator.gainAt(xToHz(x));
		state.curve.append(QPointF(x, dbToY(db)));
	}

	state.nodePositions.reserve(int(nodeList.size()));
	for (const FilterNode& node : nodeList)
		state.nodePositions.append(QPointF(hzToX(node.freq), dbToY(node.dbGain)));

	// Vertical grid: decade series 20..20k; majors at 100/1k/10k plus the
	// window edges carry labels.
	const double majors[] = { 20, 100, 1000, 10000, 20000 };
	const double minors[] = { 50, 200, 500, 2000, 5000 };
	for (double hz : majors)
	{
		GraphicEQPlotState::GridLine line;
		line.pos = hzToX(hz);
		line.major = true;
		line.label = hzLabel(hz);
		state.vertical.append(line);
	}
	for (double hz : minors)
	{
		GraphicEQPlotState::GridLine line;
		line.pos = hzToX(hz);
		line.major = false;
		line.label = hzLabel(hz);
		state.vertical.append(line);
	}

	// Horizontal grid: a "nice" dB step aiming for ~5 rows.
	double stepDb = spanDb / 5.0;
	const double base = std::pow(10.0, std::floor(std::log10(stepDb)));
	if (stepDb >= 5.0 * base)
		stepDb = 5.0 * base;
	else if (stepDb >= 2.0 * base)
		stepDb = 2.0 * base;
	else
		stepDb = base;
	const double firstDb = std::floor(topDb / stepDb) * stepDb;
	for (double db = firstDb; db > topDb - spanDb; db -= stepDb)
	{
		GraphicEQPlotState::GridLine line;
		line.pos = dbToY(db);
		if (line.pos < state.plotRect.top() - 0.5 || line.pos > state.plotRect.bottom() + 0.5)
			continue;
		const double remainder = std::fmod(std::abs(db) + 1e-6, stepDb * 2.0);
		line.major = remainder < 1e-3;
		line.label = dbLabel(db);
		state.horizontal.append(line);
	}

	if (cursorInside && dragged == -1 && !panning)
	{
		state.cursorValid = true;
		state.cursor = cursorPos;
		const double hz = xToHz(cursorPos.x());
		state.cursorText = QStringLiteral("%1 Hz  %2 dB")
			.arg(hz >= 1000.0 ? QString::number(hz, 'f', 0) : QString::number(hz, 'f', 1),
				QString::number(yToDb(cursorPos.y()), 'f', 1));
	}

	return state;
}

void GraphicEQPlotWidget::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::TextAntialiasing);
	SkinManager::instance()->paintGraphicEqPlot(painter, buildState());
}

void GraphicEQPlotWidget::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	if (event->button() == Qt::RightButton)
	{
		panning = true;
		panAnchor = event->position();
		return;
	}
	if (event->button() != Qt::LeftButton)
	{
		event->ignore();
		return;
	}

	const int hit = nodeAt(event->position());
	if (hit >= 0)
	{
		if (event->modifiers() & Qt::ControlModifier)
		{
			if (selection.contains(hit))
				selection.remove(hit);
			else
				selection.insert(hit);
		}
		else if (!selection.contains(hit))
		{
			selection = { hit };
		}
		setFocusedNode(hit);
		dragged = hit;
	}
	else if (!(event->modifiers() & Qt::ControlModifier))
	{
		selection.clear();
	}
	update();
}

void GraphicEQPlotWidget::mouseMoveEvent(QMouseEvent* event)
{
	cursorPos = event->position();
	cursorInside = plotRect().contains(cursorPos);

	if (panning)
	{
		const double dbPerPx = spanDb / plotRect().height();
		setFrame(topDb + (event->position().y() - panAnchor.y()) * dbPerPx, spanDb);
		panAnchor = event->position();
		return;
	}

	if (dragged >= 0)
	{
		double hz = xToHz(event->position().x());
		hz = hz >= 10000.0 ? std::round(hz) : std::round(hz * 10.0) / 10.0;
		double db = yToDb(event->position().y());
		db = spanDb <= 30.0 ? std::round(db * 100.0) / 100.0 : std::round(db * 10.0) / 10.0;
		const int newIndex = moveNode(dragged, hz, db);
		if (focused == dragged && newIndex != dragged)
			setFocusedNode(newIndex);
		else
			emit focusedNodeChanged(focused); // refresh readout values live
		dragged = newIndex;
		update();
		emit nodesEdited();
		return;
	}

	const int hit = nodeAt(event->position());
	if (hit != hovered)
	{
		hovered = hit;
		update();
	}
	else if (cursorInside)
	{
		update();
	}
}

void GraphicEQPlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::RightButton)
	{
		panning = false;
		return;
	}
	if (event->button() == Qt::LeftButton)
		dragged = -1;
}

void GraphicEQPlotWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton || bands != -1)
		return;
	if (nodeAt(event->position()) >= 0 || !plotRect().contains(event->position()))
		return;
	addNodeAt(event->position());
}

void GraphicEQPlotWidget::wheelEvent(QWheelEvent* event)
{
	event->accept();
	// dB zoom anchored at the cursor's dB value; the frequency axis is pinned.
	const double anchorDb = yToDb(event->position().y());
	const double factor = std::pow(1.0015, -event->angleDelta().y());
	const double newSpan = qBound(minSpanDb, spanDb * factor, maxSpanDb);
	const double anchorRatio = (topDb - anchorDb) / spanDb;
	setFrame(anchorDb + anchorRatio * newSpan, newSpan);
}

void GraphicEQPlotWidget::keyPressEvent(QKeyEvent* event)
{
	if (focused >= 0 && focused < int(nodeList.size()))
	{
		const FilterNode node = nodeList[size_t(focused)];
		switch (event->key())
		{
		case Qt::Key_Up:
			setNodeValues(focused, node.freq, node.dbGain + 1.0);
			return;
		case Qt::Key_Down:
			setNodeValues(focused, node.freq, node.dbGain - 1.0);
			return;
		case Qt::Key_Left:
			if (bands == -1)
				setNodeValues(focused, node.freq - 1.0, node.dbGain);
			return;
		case Qt::Key_Right:
			if (bands == -1)
				setNodeValues(focused, node.freq + 1.0, node.dbGain);
			return;
		case Qt::Key_Delete:
		case Qt::Key_Backspace:
			if (bands == -1)
				removeSelected();
			return;
		default:
			break;
		}
	}
	if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier))
	{
		selection.clear();
		for (int i = 0; i < int(nodeList.size()); i++)
			selection.insert(i);
		update();
		return;
	}
	QWidget::keyPressEvent(event);
}

void GraphicEQPlotWidget::leaveEvent(QEvent*)
{
	cursorInside = false;
	hovered = -1;
	update();
}

void GraphicEQPlotWidget::focusInEvent(QFocusEvent* event)
{
	QWidget::focusInEvent(event);
	update();
}

void GraphicEQPlotWidget::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
	update();
}
