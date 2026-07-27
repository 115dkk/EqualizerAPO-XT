#pragma once

#include <vector>

#include <QWidget>

#include "filters/velvet/Processor.h"

class VelvetImpulsePreview : public QWidget
{
	Q_OBJECT

public:
	explicit VelvetImpulsePreview(QWidget* parent = nullptr);
	void setImpulse(const std::vector<velvet::Tap>& storage,
		std::size_t count, std::size_t tailSamples);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	std::vector<velvet::Tap> taps;
	std::size_t tailSamples = 1;
};
