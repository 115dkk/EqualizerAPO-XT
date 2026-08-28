/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDial>

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

	QString text;
	bool bipolar = false;
};
