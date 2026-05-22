#include "EqGraphView.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPainterPath>
#include <QVector>

#include "Editor/SkinManager.h"

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

}

EqGraphView::EqGraphView(QWidget* parent)
	: QWidget(parent)
{
	setMinimumHeight(180);
	setObjectName(QStringLiteral("EqGraphView"));
}

void EqGraphView::setNodes(const std::vector<FilterNode>& nodes, unsigned sampleRate, const QString& channel)
{
	currentNodes = nodes;
	currentSampleRate = sampleRate;
	currentChannel = channel.isEmpty() ? QStringLiteral("All") : channel;
	update();
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
	QRectF graphRect = rect().adjusted(18, 16, -18, -28);

	QRectF bgRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	qreal radius = qMax(0, tokens.borderRadius - 2);
	QPainterPath bgPath;
	bgPath.addRoundedRect(bgRect, radius, radius);
	painter.fillPath(bgPath, QColor(tokens.graph));
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.drawPath(bgPath);
	painter.setClipPath(bgPath);

	QPen gridPen(QColor(tokens.border), 1);
	gridPen.setCosmetic(true);
	painter.setPen(gridPen);

	const QVector<double> frequencyTicks = {20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0};
	for (double hz : frequencyTicks)
	{
		const double t = std::log(hz / MinHz) / std::log(MaxHz / MinHz);
		double x = graphRect.left() + graphRect.width() * t;
		painter.drawLine(QPointF(x, graphRect.top()), QPointF(x, graphRect.bottom()));
	}

	GainIterator gainIterator(currentNodes);
	QVector<double> sampledDb;
	sampledDb.reserve(qMax(2, static_cast<int>(graphRect.width())));
	double maxAbsDb = 0.0;
	for (int x = static_cast<int>(graphRect.left()); x <= static_cast<int>(graphRect.right()); x++)
	{
		const double db = gainIterator.gainAt(xToHz(graphRect, x));
		const double finiteDb = std::isfinite(db) ? db : -120.0;
		sampledDb.append(finiteDb);
		maxAbsDb = std::max(maxAbsDb, std::abs(finiteDb));
	}

	const double rangeDb = qBound(12.0, std::ceil(maxAbsDb / 6.0) * 6.0, 60.0);
	const double minDb = -rangeDb;
	const double maxDb = rangeDb;
	for (int db = static_cast<int>(minDb); db <= static_cast<int>(maxDb); db += 6)
	{
		double y = dbToY(graphRect, db, minDb, maxDb);
		painter.drawLine(QPointF(graphRect.left(), y), QPointF(graphRect.right(), y));
	}

	QPen zeroPen(QColor(tokens.mutedText), 1.4);
	zeroPen.setCosmetic(true);
	painter.setPen(zeroPen);
	double zeroY = dbToY(graphRect, 0.0, minDb, maxDb);
	painter.drawLine(QPointF(graphRect.left(), zeroY), QPointF(graphRect.right(), zeroY));

	QPainterPath curve;
	bool first = true;
	for (int i = 0; i < sampledDb.size(); i++)
	{
		const double x = graphRect.left() + i;
		const double y = dbToY(graphRect, sampledDb[i], minDb, maxDb);
		if (first)
		{
			curve.moveTo(x, y);
			first = false;
		}
		else
		{
			curve.lineTo(x, y);
		}
	}

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

	painter.setPen(QColor(tokens.mutedText));
	QFont labelFont = font();
	labelFont.setPointSizeF(qMax(7.0, labelFont.pointSizeF() - 1.0));
	painter.setFont(labelFont);
	painter.drawText(QRectF(graphRect.left(), graphRect.bottom() + 3, graphRect.width(), 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("20 Hz"));
	painter.drawText(QRectF(graphRect.left(), graphRect.bottom() + 3, graphRect.width(), 18), Qt::AlignCenter, currentSampleRate == 0 ? currentChannel : QStringLiteral("%1 - %2 Hz").arg(currentChannel).arg(currentSampleRate));
	painter.drawText(QRectF(graphRect.left(), graphRect.bottom() + 3, graphRect.width(), 18), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("20 kHz"));

	painter.drawText(QRectF(graphRect.left() + 4, graphRect.top() + 3, 70, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("+%1 dB").arg(maxDb, 0, 'f', 0));
	painter.drawText(QRectF(graphRect.left() + 4, graphRect.bottom() - 21, 70, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("%1 dB").arg(minDb, 0, 'f', 0));
}
