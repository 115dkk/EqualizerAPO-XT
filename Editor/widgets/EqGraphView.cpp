#include "EqGraphView.h"

#include <algorithm>
#include <cmath>

#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"

namespace
{
constexpr double MinHz = 20.0;
constexpr double MaxHz = 20000.0;

double xToHz(const QRectF& graphRect, double x)
{
	const double t = graphRect.width() <= 0.0 ? 0.0 : (x - graphRect.left()) / graphRect.width();
	return MinHz * std::pow(MaxHz / MinHz, t);
}

double dbToY(const QRectF& graphRect, double db, double minDb, double maxDb)
{
	const double bounded = qBound(minDb, db, maxDb);
	const double t = (maxDb - bounded) / (maxDb - minDb);
	return graphRect.top() + graphRect.height() * t;
}

QString hzLabel(double hz)
{
	if (hz >= 1000.0)
		return QStringLiteral("%1k").arg(hz / 1000.0, 0, 'g', 3);
	return QString::number(hz, 'g', 3);
}
}

EqGraphView::EqGraphView(QWidget* parent)
	: QWidget(parent)
{
	setMinimumHeight(130);
	setObjectName(QStringLiteral("EqGraphView"));
	// The cursor readout follows the pointer without a button held.
	setMouseTracking(true);
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		update();
	});
}

void EqGraphView::setNodes(const std::vector<FilterNode>& nodes, unsigned sampleRate, const QString& channel)
{
	currentNodes = nodes;
	currentSampleRate = sampleRate;
	currentChannel = channel.isEmpty() ? QStringLiteral("All") : channel;
	curveDirty = true;
	update();
}

void EqGraphView::setChannel(const QString& channel)
{
	// Only the label changes; the cached curve geometry stays valid.
	currentChannel = channel;
	update();
}

void EqGraphView::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	curveDirty = true;
}

const QString& EqGraphView::channel() const
{
	return currentChannel;
}

QSize EqGraphView::sizeHint() const
{
	// Keeps the analysis dock's default height modest; the graph auto-fits its
	// data, so users who want a taller view can simply drag the dock splitter.
	return QSize(960, 190);
}

void EqGraphView::setPreviewCursor(double xRatio)
{
	previewCursorRatio = xRatio;
	hoverValue = 1.0;
	update();
}

QRectF EqGraphView::plotRect() const
{
	return QRectF(rect()).adjusted(18, 16, -18, -28);
}

void EqGraphView::mouseMoveEvent(QMouseEvent* event)
{
	const QRectF graphRect = plotRect();
	cursorValid = graphRect.contains(event->position());
	cursorPos = event->position();
	update();
	QWidget::mouseMoveEvent(event);
}

void EqGraphView::enterEvent(QEnterEvent* event)
{
	animateHover(1.0, 150);
	QWidget::enterEvent(event);
}

void EqGraphView::leaveEvent(QEvent* event)
{
	cursorValid = false;
	animateHover(0.0, 110);
	QWidget::leaveEvent(event);
}

void EqGraphView::animateHover(double target, int duration)
{
	if (hoverAnimation == nullptr)
	{
		hoverAnimation = new QVariantAnimation(this);
		hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
		connect(hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
			hoverValue = value.toDouble();
			update();
		});
	}
	// Interruptible: retarget from the current value.
	hoverAnimation->stop();
	hoverAnimation->setDuration(duration);
	hoverAnimation->setStartValue(hoverValue);
	hoverAnimation->setEndValue(target);
	hoverAnimation->start();
}

void EqGraphView::rebuildCurve(const QRectF& graphRect)
{
	GainIterator gainIterator(currentNodes);
	cachedDb.clear();
	cachedDb.reserve(qMax(2, static_cast<int>(graphRect.width())));
	double maxAbsDb = 0.0;
	for (int x = static_cast<int>(graphRect.left()); x <= static_cast<int>(graphRect.right()); x++)
	{
		const double db = gainIterator.gainAt(xToHz(graphRect, x));
		const double finiteDb = std::isfinite(db) ? db : -120.0;
		cachedDb.append(finiteDb);
		maxAbsDb = std::max(maxAbsDb, std::abs(finiteDb));
	}

	const double rangeDb = qBound(12.0, std::ceil(maxAbsDb / 6.0) * 6.0, 60.0);
	cachedMinDb = -rangeDb;
	cachedMaxDb = rangeDb;
	cachedZeroY = dbToY(graphRect, 0.0, cachedMinDb, cachedMaxDb);
	cachedClipping = false;

	cachedCurve.clear();
	cachedCurve.reserve(cachedDb.size());
	for (int i = 0; i < cachedDb.size(); i++)
	{
		cachedCurve.append(QPointF(graphRect.left() + i,
			dbToY(graphRect, cachedDb[i], cachedMinDb, cachedMaxDb)));
		if (cachedDb[i] > 0.05)
			cachedClipping = true;
	}

	cachedSize = size();
	cachedGraphRect = graphRect;
	curveDirty = false;
}

void EqGraphView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);

	const QRectF graphRect = plotRect();
	if (graphRect.width() < 2 || graphRect.height() < 2)
		return;

	if (curveDirty || cachedSize != size() || cachedGraphRect != graphRect)
		rebuildCurve(graphRect);

	AnalysisGraphState state;
	state.rect = rect();
	state.plotRect = graphRect;
	state.curve = cachedCurve;
	state.zeroY = cachedZeroY;
	state.minDb = cachedMinDb;
	state.maxDb = cachedMaxDb;
	state.clipping = cachedClipping;
	state.hover = hoverValue;
	state.channelText = currentSampleRate == 0
		? currentChannel
		: QStringLiteral("%1 - %2 Hz").arg(currentChannel).arg(currentSampleRate);

	const QVector<double> frequencyTicks = {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0};
	for (double hz : frequencyTicks)
	{
		const double t = std::log(hz / MinHz) / std::log(MaxHz / MinHz);
		AnalysisGraphState::GridLine line;
		line.pos = graphRect.left() + graphRect.width() * t;
		line.label = hzLabel(hz);
		line.major = hz == 100.0 || hz == 1000.0 || hz == 10000.0;
		state.vertical.append(line);
	}
	for (int db = static_cast<int>(cachedMinDb); db <= static_cast<int>(cachedMaxDb); db += 6)
	{
		AnalysisGraphState::GridLine line;
		line.pos = dbToY(graphRect, db, cachedMinDb, cachedMaxDb);
		line.label = db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
		line.major = db == 0;
		state.horizontal.append(line);
	}

	// Cursor readout: a live pointer, or the gallery's pinned preview.
	double cursorX = -1.0;
	if (previewCursorRatio >= 0.0)
		cursorX = graphRect.left() + graphRect.width() * previewCursorRatio;
	else if (cursorValid)
		cursorX = qBound(graphRect.left(), cursorPos.x(), graphRect.right());
	if (cursorX >= graphRect.left() && !cachedDb.isEmpty())
	{
		const int index = qBound(0, static_cast<int>(cursorX - graphRect.left()), cachedDb.size() - 1);
		const double hz = xToHz(graphRect, cursorX);
		state.cursorValid = true;
		state.cursor = QPointF(cursorX, dbToY(graphRect, cachedDb[index], cachedMinDb, cachedMaxDb));
		state.curveYAtCursor = state.cursor.y();
		state.cursorText = QStringLiteral("%1 Hz  %2 dB")
			.arg(hz >= 1000.0 ? QString::number(hz / 1000.0, 'f', 2) + QStringLiteral("k") : QString::number(hz, 'f', 0))
			.arg(cachedDb[index], 0, 'f', 1);
	}

	SkinManager::instance()->paintAnalysisGraph(painter, state);
}
