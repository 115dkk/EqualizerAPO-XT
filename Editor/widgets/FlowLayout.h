/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A left-to-right layout that wraps its items onto the next line when they
	run out of horizontal room, like word wrap for widgets. Used by the
	in-card device chips so a machine with many endpoints grows the card
	downward instead of overflowing the row (the gallery fails a row that
	needs a horizontal scroll bar). This is the canonical Qt FlowLayout
	example, trimmed to what the cards need.
*/

#pragma once

#include <QLayout>
#include <QList>
#include <QRect>
#include <QStyle>

class FlowLayout : public QLayout
{
public:
	explicit FlowLayout(QWidget* parent, int margin = 0, int hSpacing = 6, int vSpacing = 6);
	explicit FlowLayout(int margin = 0, int hSpacing = 6, int vSpacing = 6);
	~FlowLayout() override;

	void addItem(QLayoutItem* item) override;
	int horizontalSpacing() const;
	int verticalSpacing() const;
	Qt::Orientations expandingDirections() const override;
	bool hasHeightForWidth() const override;
	int heightForWidth(int width) const override;
	int count() const override;
	QLayoutItem* itemAt(int index) const override;
	QSize minimumSize() const override;
	void setGeometry(const QRect& rect) override;
	QSize sizeHint() const override;
	QLayoutItem* takeAt(int index) override;

private:
	int doLayout(const QRect& rect, bool testOnly) const;
	int smartSpacing(QStyle::PixelMetric pm) const;

	QList<QLayoutItem*> itemList;
	int m_hSpace;
	int m_vSpace;
};
