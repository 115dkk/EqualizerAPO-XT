/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <memory>

#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

#include "Editor/analysis/AnalysisMetric.h"
#include "Editor/analysis/AnalysisResponse.h"
#include "Editor/analysis/ResponseCurveBuilder.h"

class QVariantAnimation;

// The analysis dock's response graph. Owns the data (the analyzer's complex
// response for the whole config), the choice of metric, the axis fit, the
// cursor tracking and the hover animation; every pixel belongs to the active
// skin through ISkin::paintAnalysisGraph.
//
// The response is held as a shared snapshot, so switching metric re-derives the
// curve from numbers already in hand - no FilterEngine run, no FFT, and nothing
// that has to wait on the analysis thread.
class EqGraphView : public QWidget
{
	Q_OBJECT

public:
	explicit EqGraphView(QWidget* parent = nullptr);

	void setResponse(const std::shared_ptr<const AnalysisResponse>& response, const QString& channel);
	void setChannel(const QString& channel);
	void setMetric(AnalysisMetric metric);
	AnalysisMetric metric() const;
	void setIncludeLatency(bool include);
	bool includeLatency() const;
	const QString& channel() const;
	QSize sizeHint() const override;

	// Skin gallery hook: a deterministic cursor readout without a real
	// pointer (hover pinned at 1, cursor at the given plot-relative ratio).
	void setPreviewCursor(double xRatio);

protected:
	void paintEvent(QPaintEvent*) override;
	void resizeEvent(QResizeEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	QRectF plotRect() const;
	AnalysisCurveRequest curveRequest(const QRectF& graphRect) const;
	void rebuildCurve(const QRectF& graphRect);
	void animateHover(double target, int duration);

	std::shared_ptr<const AnalysisResponse> currentResponse = std::make_shared<AnalysisResponse>();
	QString currentChannel = QStringLiteral("All");
	AnalysisMetric currentMetric = AnalysisMetric::MagnitudeDb;
	bool currentIncludeLatency = false;

	// Cached curve geometry. Rebuilt only when the response, the metric, the
	// latency choice or the widget dimensions change; skin or channel-label
	// changes reuse the cache. Nothing here is computed in paintEvent.
	bool curveDirty = true;
	QSize cachedSize;
	QRectF cachedGraphRect;
	AnalysisCurve cachedCurve;
	QVector<QPolygonF> cachedSegments;
	double cachedZeroY = 0.0;
	bool cachedZeroVisible = false;

	bool cursorValid = false;
	QPointF cursorPos;
	double previewCursorRatio = -1.0;
	double hoverValue = 0.0;
	QVariantAnimation* hoverAnimation = nullptr;
};
