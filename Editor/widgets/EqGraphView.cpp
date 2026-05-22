#include "EqGraphView.h"

#include <QPainter>
#include <QPainterPath>

#include "Editor/SkinManager.h"

EqGraphView::EqGraphView(QWidget* parent)
	: QWidget(parent)
{
	setMinimumHeight(180);
	setObjectName(QStringLiteral("EqGraphView"));
}

void EqGraphView::setChannel(const QString& channel)
{
	currentChannel = channel;
	update();
}

QString EqGraphView::channel() const
{
	return currentChannel;
}

QSize EqGraphView::sizeHint() const
{
	return QSize(960, 280);
}

void EqGraphView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	QRectF graphRect = rect().adjusted(16, 14, -16, -24);
	painter.fillRect(rect(), QColor(tokens.graph));

	QPen gridPen(QColor(tokens.border), 1);
	gridPen.setCosmetic(true);
	painter.setPen(gridPen);
	for (int i = 0; i <= 6; i++)
	{
		double x = graphRect.left() + graphRect.width() * i / 6.0;
		painter.drawLine(QPointF(x, graphRect.top()), QPointF(x, graphRect.bottom()));
	}
	for (int i = 0; i <= 4; i++)
	{
		double y = graphRect.top() + graphRect.height() * i / 4.0;
		painter.drawLine(QPointF(graphRect.left(), y), QPointF(graphRect.right(), y));
	}

	QPen zeroPen(QColor(tokens.mutedText), 1.4);
	zeroPen.setCosmetic(true);
	painter.setPen(zeroPen);
	double zeroY = graphRect.center().y();
	painter.drawLine(QPointF(graphRect.left(), zeroY), QPointF(graphRect.right(), zeroY));

	QPainterPath curve;
	curve.moveTo(graphRect.left(), zeroY + graphRect.height() * 0.05);
	curve.cubicTo(
		graphRect.left() + graphRect.width() * 0.18, zeroY - graphRect.height() * 0.16,
		graphRect.left() + graphRect.width() * 0.28, zeroY + graphRect.height() * 0.10,
		graphRect.left() + graphRect.width() * 0.38, zeroY + graphRect.height() * 0.03);
	curve.cubicTo(
		graphRect.left() + graphRect.width() * 0.52, zeroY - graphRect.height() * 0.18,
		graphRect.left() + graphRect.width() * 0.64, zeroY - graphRect.height() * 0.22,
		graphRect.left() + graphRect.width() * 0.74, zeroY - graphRect.height() * 0.06);
	curve.cubicTo(
		graphRect.left() + graphRect.width() * 0.84, zeroY + graphRect.height() * 0.08,
		graphRect.left() + graphRect.width() * 0.92, zeroY + graphRect.height() * 0.02,
		graphRect.right(), zeroY);

	QPainterPath fillPath = curve;
	fillPath.lineTo(graphRect.right(), zeroY);
	fillPath.lineTo(graphRect.left(), zeroY);
	fillPath.closeSubpath();
	QColor fill(tokens.accent);
	fill.setAlpha(SkinManager::instance()->isDark() ? 30 : 18);
	painter.fillPath(fillPath, fill);

	QPen curvePen(QColor(tokens.accent), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	curvePen.setCosmetic(true);
	painter.setPen(curvePen);
	painter.drawPath(curve);

	QVector<QPointF> nodes = {
		QPointF(graphRect.left() + graphRect.width() * 0.32, zeroY + graphRect.height() * 0.08),
		QPointF(graphRect.left() + graphRect.width() * 0.58, zeroY - graphRect.height() * 0.21),
		QPointF(graphRect.left() + graphRect.width() * 0.78, zeroY - graphRect.height() * 0.03)
	};
	QVector<QColor> nodeColors = { QColor("#22c55e"), QColor("#6366f1"), QColor("#ec4899") };
	painter.setPen(QPen(QColor(tokens.graph), 2));
	for (int i = 0; i < nodes.size(); i++)
	{
		painter.setBrush(nodeColors[i]);
		painter.drawEllipse(nodes[i], 6, 6);
	}

	painter.setPen(QColor(tokens.mutedText));
	QFont labelFont = font();
	labelFont.setPointSizeF(qMax(7.0, labelFont.pointSizeF() - 1.0));
	painter.setFont(labelFont);
	painter.drawText(QRectF(graphRect.left(), graphRect.bottom() + 3, graphRect.width(), 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("20 Hz"));
	painter.drawText(QRectF(graphRect.left(), graphRect.bottom() + 3, graphRect.width(), 18), Qt::AlignCenter, currentChannel);
	painter.drawText(QRectF(graphRect.left(), graphRect.bottom() + 3, graphRect.width(), 18), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("20 kHz"));
}
