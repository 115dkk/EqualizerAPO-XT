#pragma once

#include <vector>

#include <QWidget>

#include "helpers/GainIterator.h"

class EqGraphView : public QWidget
{
	Q_OBJECT

public:
	explicit EqGraphView(QWidget* parent = nullptr);

	void setNodes(const std::vector<FilterNode>& nodes, unsigned sampleRate, const QString& channel);
	void setChannel(const QString& channel);
	QString channel() const;
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;

private:
	std::vector<FilterNode> currentNodes;
	QString currentChannel = QStringLiteral("All");
	unsigned currentSampleRate = 0;
};
