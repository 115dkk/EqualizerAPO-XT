/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	One channel-fill rail of the modern VST card: a row of slot cells, each
	assigning a config channel to one negotiated bus slot, mounted inside the
	card (input rail under the header, output rail under the body). The input
	rail additionally carries the fold latch when both rails exist.

	The rail and its cells own all behavior - the channel popup menu,
	keyboard access, focus, accessibility - and hand painting to the active
	skin (ISkin::paintVstSlotFillCell / ISkin::paintVstSlotFillRail), the
	same split VSTBusStrip uses. The host (VSTCardEditor) owns the
	semantics through VSTSlotFillModel: which rails exist, the effective
	values, and what a pick means.
*/

#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

#include "Editor/skins/ISkin.h"

class VSTSlotFillRail;

// One slot cell. Click / Space / Enter / Down opens the channel menu.
class VSTSlotFillCell : public QWidget
{
	Q_OBJECT

public:
	VSTSlotFillCell(bool output, int slot, QWidget* parent = nullptr);

	void setContent(const QString& role, const QString& value,
		bool silent, bool defaulted, bool missing);
	void setChannelChoices(const QStringList& names);
	// The menu this cell would open: the selected channels in order, then
	// the silence/discard entry ("-"). Read by the --selftest-vst fill gate.
	QStringList channelChoices() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void picked(int slot, const QString& value);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;

private:
	void openMenu();
	void refreshAccessibleValue();

	bool output = false;
	int slot = 0;
	QString role;
	QString value;
	bool silent = false;
	bool defaulted = false;
	bool missing = false;
	QStringList choices;
	bool hovered = false;
	bool pressed = false;
	bool menuOpen = false;
};

class VSTSlotFillRail : public QWidget
{
	Q_OBJECT

public:
	struct CellData
	{
		QString role;
		QString value;
		bool silent = false;
		bool defaulted = false;
		bool missing = false;
	};

	explicit VSTSlotFillRail(bool output, QWidget* parent = nullptr);

	void setCells(const QList<CellData>& cells);
	void setChannelChoices(const QStringList& names);
	// The fold latch; only ever shown on the input rail, and only while the
	// card has both rails (a single rail does not fold).
	void setLatchVisible(bool visible);
	void setCollapsed(bool collapsed);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void slotPicked(int slot, const QString& value);
	void latchToggled();

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void relayout();
	int railHeight() const;

	bool output = false;
	bool collapsed = false;
	QList<VSTSlotFillCell*> cells;
	QStringList choices;
	// Transparent interactive child; the rail paints its visuals through
	// ISkin::paintVstSlotFillRail so the latch stays a rail-frame affordance
	// rather than one more cell.
	QWidget* latch = nullptr;
	bool latchHovered = false;
	bool latchPressed = false;
};
