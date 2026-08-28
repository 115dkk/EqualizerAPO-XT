/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

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

// Decade markers plus the two ends. Any tick above the graph's upper limit is
// dropped rather than crowded against the right edge, which is what happens at
// sample rates whose Nyquist falls below 20 kHz.
const double FrequencyTicks[] = {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0};

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

void EqGraphView::setResponse(const std::shared_ptr<const AnalysisResponse>& response, const QString& channel)
{
	currentResponse = response ? response : std::make_shared<AnalysisResponse>();
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

void EqGraphView::setMetric(AnalysisMetric metric)
{
	if (currentMetric == metric)
		return;
	// Re-derived from the response already in hand. Deliberately does not touch
	// the analysis thread: a metric change is a display choice, not a new
	// measurement.
	currentMetric = metric;
	curveDirty = true;
	update();
}

AnalysisMetric EqGraphView::metric() const
{
	return currentMetric;
}

void EqGraphView::setIncludeLatency(bool include)
{
	if (currentIncludeLatency == include)
		return;
	currentIncludeLatency = include;
	curveDirty = true;
	update();
}

bool EqGraphView::includeLatency() const
{
	return currentIncludeLatency;
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

AnalysisCurveRequest EqGraphView::curveRequest(const QRectF& graphRect) const
{
	AnalysisCurveRequest request;
	request.metric = currentMetric;
	request.includeLatency = currentIncludeLatency;
	// One column per pixel, both ends included - the same set of x positions
	// the graph has always sampled.
	request.columnCount = static_cast<int>(graphRect.right()) - static_cast<int>(graphRect.left()) + 1;
	request.minHz = MinHz;
	request.maxHz = analysisUpperFrequency(*currentResponse);
	return request;
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
	const AnalysisCurveRequest request = curveRequest(graphRect);
	cachedCurve = buildAnalysisCurve(*currentResponse, request);
	cachedSegments = buildCurveSegments(cachedCurve.values, graphRect,
		cachedCurve.minimum, cachedCurve.maximum);
	cachedZeroY = analysisValueToY(graphRect, 0.0, cachedCurve.minimum, cachedCurve.maximum);
	cachedZeroVisible = cachedCurve.minimum <= 0.0 && cachedCurve.maximum >= 0.0;

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

	const AnalysisCurveRequest request = curveRequest(graphRect);

	AnalysisGraphState state;
	state.rect = rect();
	state.plotRect = graphRect;
	state.metric = cachedCurve.metric;
	state.curves = cachedSegments;
	state.zeroY = cachedZeroY;
	state.zeroVisible = cachedZeroVisible;
	state.minimum = cachedCurve.minimum;
	state.maximum = cachedCurve.maximum;
	state.unit = cachedCurve.unit;
	state.clipping = cachedCurve.clipping;
	state.hover = hoverValue;
	state.topValueText = cachedCurve.topLabel;
	state.bottomValueText = cachedCurve.bottomLabel;
	state.spanValueText = cachedCurve.spanText;
	state.leftFooterText = analysisFrequencyCaption(request.minHz);
	state.rightFooterText = analysisFrequencyCaption(request.maxHz);
	state.channelText = currentResponse->sampleRate == 0
		? currentChannel
		: QStringLiteral("%1 - %2 Hz").arg(currentChannel).arg(currentResponse->sampleRate);
	if (currentResponse->frozenDynamicResponse)
		state.channelText += tr(" · frozen Velvet snapshot");

	for (double hz : FrequencyTicks)
	{
		if (hz > request.maxHz)
			continue;
		const double t = std::log(hz / request.minHz) / std::log(request.maxHz / request.minHz);
		AnalysisGraphState::GridLine line;
		line.pos = graphRect.left() + graphRect.width() * t;
		line.label = hzLabel(hz);
		line.major = hz == 100.0 || hz == 1000.0 || hz == 10000.0;
		state.vertical.append(line);
	}
	for (const AnalysisCurveTick& tick : cachedCurve.ticks)
	{
		AnalysisGraphState::GridLine line;
		line.pos = analysisValueToY(graphRect, tick.value, cachedCurve.minimum, cachedCurve.maximum);
		line.label = tick.label;
		line.major = tick.major;
		state.horizontal.append(line);
	}

	// Cursor readout: a live pointer, or the gallery's pinned preview.
	double cursorX = -1.0;
	if (previewCursorRatio >= 0.0)
		cursorX = graphRect.left() + graphRect.width() * previewCursorRatio;
	else if (cursorValid)
		cursorX = qBound(graphRect.left(), cursorPos.x(), graphRect.right());
	if (cursorX >= graphRect.left() && !cachedCurve.values.isEmpty())
	{
		const int index = qBound(0, static_cast<int>(cursorX - graphRect.left()), cachedCurve.values.size() - 1);
		const double value = cachedCurve.values[index];
		const double t = graphRect.width() <= 0.0 ? 0.0 : (cursorX - graphRect.left()) / graphRect.width();
		const double hz = request.minHz * std::pow(request.maxHz / request.minHz, t);
		// A column the metric has no value for gets a crosshair and a frequency
		// but no reading, rather than a plausible-looking number.
		state.cursorValid = true;
		state.cursor = QPointF(cursorX,
			analysisValueToY(graphRect, value, cachedCurve.minimum, cachedCurve.maximum));
		state.curveYAtCursor = state.cursor.y();
		if (std::isfinite(value))
		{
			state.cursorText = QStringLiteral("%1  %2")
				.arg(hz >= 1000.0
						? QStringLiteral("%1 kHz").arg(hz / 1000.0, 0, 'f', 2)
						: QStringLiteral("%1 Hz").arg(hz, 0, 'f', 0),
					cachedCurve.formatValue(value));
		}
	}

	SkinManager::instance()->paintAnalysisGraph(painter, state);
}
