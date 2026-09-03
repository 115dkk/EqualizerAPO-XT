/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDial>

enum class KnobGesture;

class AudioKnob : public QDial
{
	Q_OBJECT

public:
	explicit AudioKnob(QWidget* parent = nullptr);

	void setValueText(const QString& text);
	// Mark this knob as bipolar (gain-style, neutral at the range centre).
	// Purely descriptive: it is forwarded to the skin through KnobState so
	// skins can render bipolar and unipolar knobs differently.
	void setBipolar(bool value);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	void setValueFromAngle(const QPointF& widgetPos);
	void setValueFromTravel(const QPointF& widgetPos, Qt::KeyboardModifiers modifiers);
	void syncGestureCursor();

	QString text;
	bool bipolar = false;
	// The gesture of the drag in progress, latched at the press (the skin
	// names it: ISkin::knobGesture) so a skin switch mid-drag cannot change
	// the law under the pointer.
	KnobGesture gesture;
	// VerticalDrag bookkeeping: the value in fractional units carried across
	// moves, and the pointer y of the last move.
	double travelValue = 0.0;
	double travelY = 0.0;
};
