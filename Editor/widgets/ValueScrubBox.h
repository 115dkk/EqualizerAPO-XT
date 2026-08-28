/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Spin boxes for command-row editors with the native up/down buttons removed.
	The value is changed by dragging
	vertically on the text ("value scrub"), by the mouse wheel, or by typing;
	skins can restyle the boxes through the valueScrub dynamic property and
	later contribute their own stepper chrome.
*/

#pragma once

#include <QDoubleSpinBox>
#include <QSpinBox>

// Event filter on a spin box's line edit that turns a vertical drag on the
// text into stepBy() calls. Horizontal drags keep their text-selection
// meaning; clicks, double clicks, focus and wheel behave as usual.
class ValueScrubController : public QObject
{
	Q_OBJECT

public:
	explicit ValueScrubController(QAbstractSpinBox* box);
	~ValueScrubController() override;

	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void endScrub();

	QAbstractSpinBox* box;
	QPoint pressPos;
	int anchorY = 0;
	bool pressed = false;
	bool scrubbing = false;
	bool textDrag = false;
};

class ValueScrubBox : public QDoubleSpinBox
{
	Q_OBJECT

public:
	explicit ValueScrubBox(QWidget* parent = nullptr);
};

class ValueScrubIntBox : public QSpinBox
{
	Q_OBJECT

public:
	explicit ValueScrubIntBox(QWidget* parent = nullptr);
};
