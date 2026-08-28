/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	A row of mutually exclusive choices in one control.
*/

#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

class QVariantAnimation;

// A small set of mutually exclusive choices, shown all at once. Used where a
// combo box would cost a click to reveal what are only two or three options,
// and where seeing the alternatives is part of understanding the control - the
// analysis graph's metric, an all-pass section's order.
//
// Every pixel belongs to the active skin through ISkin::paintSegmentedControl.
// Nothing here is styled by QSS: a stylesheet cannot express a moving indicator
// or a pressed state, and starting from one would have frozen the control into
// a flat box with a border.
class SegmentedControl : public QWidget
{
	Q_OBJECT

public:
	explicit SegmentedControl(QWidget* parent = nullptr);

	void setLabels(const QStringList& labels);
	const QStringList& labels() const;
	void setCurrentIndex(int index);
	int currentIndex() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	// Skin gallery hook: a deterministic selection and hover without a pointer
	// and without a running animation, so a rendered shot is the same bytes
	// every time instead of catching the indicator mid-travel.
	void setPreviewState(int selectedIndex, int hoveredIndex);

signals:
	// Emitted for a user's choice and for setCurrentIndex alike; callers that
	// must not recurse block signals around the programmatic call, as elsewhere
	// in this Editor.
	void currentIndexChanged(int index);

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;

private:
	int indexAt(const QPointF& position) const;
	void animateSelectionTo(int index);

	QStringList segmentLabels;
	int selected = 0;
	int hovered = -1;
	int pressed = -1;
	double selectionPosition = 0.0;
	QVariantAnimation* selectionAnimation = nullptr;
};
