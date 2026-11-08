/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AudioKnob.h"

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

void AudioKnob::setValueFromTurn(const QPointF& widgetPos, Qt::KeyboardModifiers modifiers)
{
	// Relative rotary drag: the knob turns by the angle the pointer swept
	// around its centre since the last move (KnobTravel::sweptDegrees), so a
	// press alone changes nothing and the indicator turns with the hand from
	// wherever it stood. The painted arc (135 degrees at the minimum,
	// clockwise to 405 at the maximum) is the whole range, a tenth of that
	// rate with Shift, and the value stops at either end.
	const QPointF to = widgetPos - QRectF(rect()).center();
	const double degrees = KnobTravel::sweptDegrees(turnFrom.x(), turnFrom.y(), to.x(), to.y());
	turnFrom = to;
	travelValue = KnobTravel::turn(travelValue, degrees, minimum(), maximum(),
		modifiers.testFlag(Qt::ShiftModifier));
	setValue(qRound(travelValue));
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
		// Grab, do not jump, in both gestures: the value only moves with
		// travel from here.
		travelValue = value();
		if (gesture == KnobGesture::VerticalDrag)
		{
			travelY = event->position().y();
		}
		else
		{
			setCursor(Qt::ClosedHandCursor);
			turnFrom = event->position() - QRectF(rect()).center();
		}
		event->accept();
		return;
	}

	QDial::mousePressEvent(event);
}

void AudioKnob::mouseDoubleClickEvent(QMouseEvent* event)
{
	// The press that came just before has grabbed the knob without moving it,
	// so the reset is the only change a double-click makes. The second press
	// then grabs again at the reset value: a hand that moves while it is
	// still down would otherwise carry on from the value before the reset
	// and undo it.
	if (event->button() == Qt::LeftButton)
	{
		emit resetRequested();
		mousePressEvent(event);
		return;
	}

	QDial::mouseDoubleClickEvent(event);
}

void AudioKnob::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		if (gesture == KnobGesture::VerticalDrag)
			setValueFromTravel(event->position(), event->modifiers());
		else
			setValueFromTurn(event->position(), event->modifiers());
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
