#include "AudioKnob.h"

#include <QtMath>
#include <QMouseEvent>
#include <QPainter>

#include "Editor/SkinManager.h"

AudioKnob::AudioKnob(QWidget* parent)
	: QDial(parent)
{
	setRange(0, 100);
	setNotchesVisible(false);
	setWrapping(false);
	setCursor(Qt::SizeVerCursor);
}

void AudioKnob::setValueText(const QString& valueText)
{
	text = valueText;
	update();
}

QSize AudioKnob::sizeHint() const
{
	return QSize(74, 74);
}

QPointF AudioKnob::pointOnArc(const QRectF& rect, double degrees) const
{
	double radians = qDegreesToRadians(degrees);
	QPointF center = rect.center();
	double radius = qMin(rect.width(), rect.height()) / 2.0;
	return QPointF(center.x() + qCos(radians) * radius, center.y() + qSin(radians) * radius);
}

void AudioKnob::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	QRectF knobRect = rect().adjusted(9, 9, -9, -9);
	int spanDegrees = 270;
	int startDegrees = 135;
	double ratio = maximum() == minimum() ? 0.0 : (value() - minimum()) / static_cast<double>(maximum() - minimum());

	QPen trackPen(QColor(tokens.border), 6, Qt::SolidLine, Qt::RoundCap);
	painter.setPen(trackPen);
	painter.drawArc(knobRect, -startDegrees * 16, -spanDegrees * 16);

	QPen valuePen(QColor(tokens.accent), 6, Qt::SolidLine, Qt::RoundCap);
	painter.setPen(valuePen);
	painter.drawArc(knobRect, -startDegrees * 16, -static_cast<int>(spanDegrees * ratio * 16));

	QColor fill(tokens.card);
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.setBrush(fill);
	painter.drawEllipse(knobRect.adjusted(6, 6, -6, -6));

	double endDegrees = startDegrees + spanDegrees * ratio;
	QPointF dot = pointOnArc(knobRect.adjusted(3, 3, -3, -3), -endDegrees);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(tokens.accent));
	painter.drawEllipse(dot, 4, 4);

	painter.setPen(QColor(tokens.text));
	QFont valueFont = font();
	valueFont.setBold(true);
	valueFont.setPointSizeF(qMax(7.0, valueFont.pointSizeF() - 1.0));
	painter.setFont(valueFont);
	painter.drawText(rect(), Qt::AlignCenter, text.isEmpty() ? QString::number(value()) : text);
}

void AudioKnob::mousePressEvent(QMouseEvent* event)
{
	dragStartY = event->globalPos().y();
	dragStartValue = value();
	QDial::mousePressEvent(event);
}

void AudioKnob::mouseMoveEvent(QMouseEvent* event)
{
	double sensitivity = event->modifiers() & Qt::ShiftModifier ? 0.15 : 1.0;
	int delta = static_cast<int>((dragStartY - event->globalPos().y()) * sensitivity);
	setValue(qBound(minimum(), dragStartValue + delta, maximum()));
}
