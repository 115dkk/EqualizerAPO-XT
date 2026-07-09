#pragma once

#include <vector>

#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QVector>
#include <QWidget>

#include "helpers/GainIterator.h"

class QVariantAnimation;

// The analysis dock's response graph. Owns the data (sampled response of the
// whole config), the axis fit, the cursor tracking and the hover animation;
// every pixel belongs to the active skin through ISkin::paintAnalysisGraph.
class EqGraphView : public QWidget
{
	Q_OBJECT

public:
	explicit EqGraphView(QWidget* parent = nullptr);

	void setNodes(const std::vector<FilterNode>& nodes, unsigned sampleRate, const QString& channel);
	void setChannel(const QString& channel);
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
	void rebuildCurve(const QRectF& graphRect);
	void animateHover(double target, int duration);

	std::vector<FilterNode> currentNodes;
	QString currentChannel = QStringLiteral("All");
	unsigned currentSampleRate = 0;

	// Cached curve geometry. Rebuilt only when the node set or the widget
	// dimensions change; skin or channel-label changes reuse the cache.
	bool curveDirty = true;
	QSize cachedSize;
	QRectF cachedGraphRect;
	QPolygonF cachedCurve;
	QVector<double> cachedDb; // per-px samples, for the cursor readout
	double cachedMinDb = 0.0;
	double cachedMaxDb = 0.0;
	double cachedZeroY = 0.0;
	bool cachedClipping = false;

	bool cursorValid = false;
	QPointF cursorPos;
	double previewCursorRatio = -1.0;
	double hoverValue = 0.0;
	QVariantAnimation* hoverAnimation = nullptr;
};
