/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A QLabel whose text is elided at paint time to the label's current width.
	File paths are the motivating case: their informative tail (the leaf
	folder / file name) must survive truncation, so the default mode is
	Qt::ElideMiddle (PatternFly/Carbon: a string whose end cannot be cut gets
	a middle ellipsis). Eliding at paint time - instead of when the text is
	set - keeps the visible portion correct across layout resizes, which was
	the defect that made set-time eliding destroy most of the path.

	setFullText also mirrors the full text into the tooltip (Carbon/PatternFly:
	truncated items expose the full string on hover); callers wanting a custom
	tooltip set it after setFullText.
*/

#pragma once

#include <QLabel>

class ElidedLabel : public QLabel
{
	Q_OBJECT

public:
	explicit ElidedLabel(QWidget* parent = nullptr);

	void setFullText(const QString& text);
	const QString& fullText() const;

	void setElideMode(Qt::TextElideMode mode);

	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QString storedText;
	Qt::TextElideMode elideMode = Qt::ElideMiddle;
};
