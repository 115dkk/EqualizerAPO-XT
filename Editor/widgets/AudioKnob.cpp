/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AudioKnob.h"

#include <QtMath>
#include <QMouseEvent>
#include <QPainter>

#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "KnobTravel.h"

AudioKnob::AudioKnob(QWidget* parent)
	: QDial(parent), gesture(KnobGesture::Rotary)
{
	setRange(0, 100);
	setNotchesVisible(false);
	setWrapping(false);
	syncGestureCursor();
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		if (!isSliderDown())
			syncGestureCursor();
		update();
	});
}

void AudioKnob::setValueText(const QString& valueText)
{
	text = valueText;
	update();
}

void AudioKnob::setBipolar(bool value)
{
	bipolar = value;
	update();
}

QSize AudioKnob::sizeHint() const
{
	return QSize(74, 74);
}

void AudioKnob::paintEvent(QPaintEvent*)
{
	// The widget owns all input handling; painting is delegated to the active
	// skin (ISkin::paintKnob) so each skin can render knobs with its own
	// philosophy.
	QPainter painter(this);

	KnobState state;
	state.value = value();
	state.minimum = minimum();
	state.maximum = maximum();
	state.ratio = maximum() == minimum() ? 0.0 : (value() - minimum()) / static_cast<double>(maximum() - minimum());
	state.bipolar = bipolar;
	state.valueText = text;
	state.enabled = isEnabled();
	state.hovered = underMouse();
	state.dragging = isSliderDown();
	state.focused = hasFocus();

	SkinManager::instance()->paintKnob(painter, rect(), state);
}

void AudioKnob::syncGestureCursor()
{
	// The resting cursor announces the gesture before the first press: a
	// hand for a knob that is grabbed and turned, a vertical arrow for a
	// drum that is rolled up and down. The rotary hand closes while dragging;
	// the arrow stays, because it already says which way the drum moves.
	setCursor(SkinManager::instance()->knobGesture() == KnobGesture::VerticalDrag
		? Qt::SizeVerCursor : Qt::OpenHandCursor);
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

void AudioKnob::setValueFromTravel(const QPointF& widgetPos, Qt::KeyboardModifiers modifiers)
{
	// Relative vertical drag: the press grabbed the drum where it was, and
	// only travel since then moves it - up to increase, KnobTravel::RangePixels
	// for the whole range, a tenth of that rate with Shift. Horizontal motion
	// is ignored, so a hand that wanders sideways does not move the value.
	// The travel is accumulated in fractional units and clamped at every move
	// (KnobTravel::advance), so the drum stops at either end and answers a
	// reversal at once. The minimal skin's drum rolls under the pointer one to
	// one because its painter derives the surface travel from the same figure.
	const double travel = travelY - widgetPos.y();
	travelY = widgetPos.y();
	travelValue = KnobTravel::advance(travelValue, travel, minimum(), maximum(),
		modifiers.testFlag(Qt::ShiftModifier));
	setValue(qRound(travelValue));
}

void AudioKnob::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		// We deliberately do not call QDial's handlers; QDial maps the cursor
		// with a different angle convention than our paintEvent, which made the
		// indicator drift and lurched the value when the button was released.
		gesture = SkinManager::instance()->knobGesture();
		setSliderDown(true);
		if (gesture == KnobGesture::VerticalDrag)
		{
			// Grab, do not jump: the value only moves with travel from here.
			travelValue = value();
			travelY = event->position().y();
		}
		else
		{
			// Rotary tracking: the knob turns to follow the cursor.
			setCursor(Qt::ClosedHandCursor);
			setValueFromAngle(event->position());
		}
		event->accept();
		return;
	}

	QDial::mousePressEvent(event);
}

void AudioKnob::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		if (gesture == KnobGesture::VerticalDrag)
			setValueFromTravel(event->position(), event->modifiers());
		else
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
		syncGestureCursor();
		event->accept();
		return;
	}

	QDial::mouseReleaseEvent(event);
}
