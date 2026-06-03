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
	setCursor(Qt::OpenHandCursor);
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		update();
	});
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
	// Qt measures arc angles counter-clockwise from 3 o'clock, so the matching
	// screen point subtracts sin for Y (screen Y grows downward). The previous
	// +sin mirrored the indicator dot vertically, so it never tracked the value
	// arc and looked like it floated on its own.
	return QPointF(center.x() + qCos(radians) * radius, center.y() - qSin(radians) * radius);
}

void AudioKnob::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	// Draw inside a centred square so the knob stays circular even when the
	// hosting widget is not square (promoted legacy dials are 100x66).
	QRectF inner = rect().adjusted(9, 9, -9, -9);
	double side = qMin(inner.width(), inner.height());
	QRectF knobRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);
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

	// Only draw centred text when an explicit value string was supplied (e.g.
	// the Preamp card). Promoted legacy dials drive a separate spin box for the
	// real value and map the dial to log-scaled steps, so painting value() here
	// would show a meaningless step count.
	if (!text.isEmpty())
	{
		painter.setPen(QColor(tokens.text));
		QFont valueFont = font();
		valueFont.setBold(true);
		valueFont.setPointSizeF(qMax(7.0, valueFont.pointSizeF() - 1.0));
		painter.setFont(valueFont);
		painter.drawText(rect(), Qt::AlignCenter, text);
	}
}

void AudioKnob::setValueFromAngle(const QPointF& widgetPos)
{
	// Map the cursor's angle around the knob centre onto the 270-degree value arc
	// so the indicator turns to follow the mouse like a physical knob. The geometry
	// matches paintEvent: the arc runs from 135 degrees (minimum, bottom-left)
	// clockwise to 405 degrees (maximum, bottom-right), with a dead zone across the
	// bottom. Screen Y grows downward, so a plain atan2 already gives this sweep.
	const QPointF center = rect().center();
	const double raw = qRadiansToDegrees(qAtan2(widgetPos.y() - center.y(), widgetPos.x() - center.x()));
	double angle = (raw < 135.0) ? raw + 360.0 : raw;   // unwrap into 135..495
	if (angle > 405.0)                                   // inside the bottom dead zone
		angle = (angle < 450.0) ? 405.0 : 135.0;        // snap to whichever end is nearer
	const double ratio = (angle - 135.0) / 270.0;
	setValue(qBound(minimum(), minimum() + static_cast<int>(qRound(ratio * (maximum() - minimum()))), maximum()));
}

void AudioKnob::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		// Rotary tracking: the knob turns to follow the cursor. We deliberately do
		// not call QDial's handlers; QDial maps the cursor with a different angle
		// convention than our paintEvent, which made the indicator drift and lurched
		// the value when the button was released.
		setSliderDown(true);
		setCursor(Qt::ClosedHandCursor);
		setValueFromAngle(event->position());
		event->accept();
		return;
	}

	QDial::mousePressEvent(event);
}

void AudioKnob::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		setValueFromAngle(event->position());
		event->accept();
		return;
	}

	QDial::mouseMoveEvent(event);
}

void AudioKnob::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		// End the gesture without deferring to QDial. QDial::mouseReleaseEvent would
		// re-run setValue() for the release position using its own angle mapping,
		// which is the sudden value surge seen when letting go of the knob.
		setSliderDown(false);
		setCursor(Qt::OpenHandCursor);
		event->accept();
		return;
	}

	QDial::mouseReleaseEvent(event);
}
