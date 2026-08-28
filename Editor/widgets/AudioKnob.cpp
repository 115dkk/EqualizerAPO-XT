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
