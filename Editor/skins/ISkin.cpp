/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Default knob rendering shared by every skin. The body is the pre-hook
	AudioKnob::paintEvent moved verbatim behind ISkin::paintKnob, so skins
	that do not override the hook keep exactly the appearance they had before
	the hook existed.
*/

#include "ISkin.h"

#include <QPainter>
#include <QtMath>

namespace
{
QPointF pointOnArc(const QRectF& rect, double degrees)
{
	double radians = qDegreesToRadians(degrees);
	QPointF center = rect.center();
	double radius = qMin(rect.width(), rect.height()) / 2.0;
	// Qt measures arc angles counter-clockwise from 3 o'clock, so the matching
	// screen point subtracts sin for Y (screen Y grows downward). The previous
	// +sin mirrored the indicator dot vertically, so it never tracked the value
	// arc and looked like it floated on its own.
	return QPointF(center.x() + qCos(radians) * radius, center.y() - qSin(radians) * radius);
}
}

void ISkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	painter.setRenderHint(QPainter::Antialiasing);

	// Draw inside a centred square so the knob stays circular even when the
	// hosting widget is not square (promoted legacy dials are 100x66).
	QRectF inner = QRectF(rect).adjusted(9, 9, -9, -9);
	double side = qMin(inner.width(), inner.height());
	QRectF knobRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);
	int spanDegrees = 270;
	int startDegrees = 135;
	double ratio = state.ratio;

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

	// Only draw centred text when an explicit value string was supplied (e.g.
	// the Preamp card). Promoted legacy dials drive a separate spin box for the
	// real value and map the dial to log-scaled steps, so painting value() here
	// would show a meaningless step count.
	if (!state.valueText.isEmpty())
	{
		painter.setPen(QColor(tokens.text));
		QFont valueFont = painter.font();
		valueFont.setBold(true);
		valueFont.setPointSizeF(qMax(7.0, valueFont.pointSizeF() - 1.0));
		painter.setFont(valueFont);
		painter.drawText(rect, Qt::AlignCenter, state.valueText);
	}
}
