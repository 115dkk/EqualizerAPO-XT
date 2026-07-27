#include "VelvetImpulsePreview.h"

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "Editor/SkinManager.h"

VelvetImpulsePreview::VelvetImpulsePreview(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("VelvetImpulsePreview"));
	setMinimumHeight(74);
	setToolTip(tr("One deterministic channel kernel. Sparse impulses are normalized to unit energy; dynamic mode crossfades to a newly generated bank."));
}

void VelvetImpulsePreview::setImpulse(const std::vector<velvet::Tap>& storage,
	std::size_t count, std::size_t tail)
{
	count = std::min(count, storage.size());
	taps.assign(storage.begin(), storage.begin() + count);
	tailSamples = std::max<std::size_t>(tail, 1);
	update();
}

QSize VelvetImpulsePreview::sizeHint() const
{
	return QSize(420, 82);
}

void VelvetImpulsePreview::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const SkinTokens tokens = SkinManager::instance()->tokens();
	const QRectF frame = rect().adjusted(0.5, 0.5, -0.5, -0.5);
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.setBrush(QColor(tokens.surface));
	painter.drawRoundedRect(frame, 5, 5);

	const QRectF plot = frame.adjusted(10, 9, -10, -9);
	const double centre = plot.center().y();
	painter.setPen(QPen(QColor(tokens.mutedText), 1));
	painter.drawLine(QPointF(plot.left(), centre), QPointF(plot.right(), centre));
	if (taps.empty())
		return;

	painter.setPen(QPen(QColor(tokens.accent), 1.5));
	for (const velvet::Tap& tap : taps)
	{
		const double x = plot.left() + plot.width()
			* static_cast<double>(tap.delay) / tailSamples;
		const double y = centre - tap.gain * plot.height() * 0.43;
		painter.drawLine(QPointF(x, centre), QPointF(x, y));
		painter.drawEllipse(QPointF(x, y), 1.7, 1.7);
	}
}
