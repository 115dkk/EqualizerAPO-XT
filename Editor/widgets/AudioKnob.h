/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDial>
#include <QPointF>

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

signals:
	// A double-click asks for the parameter's default. The knob does not know
	// it: a knob position is not always a value (the Delay knob's lowest step
	// is 1 ms, its default 0 ms), so the card editor sets the value itself.
	void resetRequested();

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
	void setValueFromTurn(const QPointF& widgetPos, Qt::KeyboardModifiers modifiers);
	void setValueFromTravel(const QPointF& widgetPos, Qt::KeyboardModifiers modifiers);
	void syncGestureCursor();

	QString text;
	bool bipolar = false;
	// The gesture of the drag in progress, latched at the press (the skin
	// names it: ISkin::knobGesture) so a skin switch mid-drag cannot change
	// the law under the pointer.
	KnobGesture gesture;
	// Drag bookkeeping, both gestures: the value in fractional units carried
	// across moves. VerticalDrag adds the pointer y of the last move, Rotary
	// the last pointer position relative to the knob's centre.
	double travelValue = 0.0;
	double travelY = 0.0;
	QPointF turnFrom;
};
