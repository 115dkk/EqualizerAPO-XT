#pragma once

#include <vector>

#include <QPainterPath>
#include <QRectF>
#include <QSize>
#include <QWidget>

#include "helpers/GainIterator.h"

class EqGraphView : public QWidget
{
	Q_OBJECT

public:
	explicit EqGraphView(QWidget* parent = nullptr);

	void setNodes(const std::vector<FilterNode>& nodes, unsigned sampleRate, const QString& channel);
	void setChannel(const QString& channel);
	const QString& channel() const;
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	std::vector<FilterNode> currentNodes;
	QString currentChannel = QStringLiteral("All");
	unsigned currentSampleRate = 0;

	// Cached curve geometry. Rebuilt only when the node set or the widget
	// dimensions change; skin or channel-label changes reuse the cached path
	// (they only affect pen colour and overlay text).
	bool curveDirty = true;
	QSize cachedSize;
	QRectF cachedGraphRect;
	QPainterPath cachedCurve;
	QPainterPath cachedFill;
	double cachedMinDb = 0.0;
	double cachedMaxDb = 0.0;
	double cachedZeroY = 0.0;
};
