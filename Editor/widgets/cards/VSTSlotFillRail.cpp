/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QAccessible>
#include <QEnterEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include "Editor/SkinManager.h"
#include "VSTSlotFillRail.h"

namespace
{
// The latch child is interaction-only: the rail paints its visuals through
// ISkin::paintVstSlotFillRail. A plain QWidget would render the application
// stylesheet's background over that painting, so this one paints nothing.
class RailLatch : public QWidget
{
public:
	using QWidget::QWidget;

protected:
	void paintEvent(QPaintEvent*) override
	{
	}
};

int cellHeight()
{
	return 20;
}

int latchWidth()
{
	// The rack treatment is the widest: its pilot LED, engraved FILL label and
	// margins must fit side by side.
	const SkinTokens& t = SkinManager::instance()->tokens();
	QFont latchFont(t.fontFamily);
	latchFont.setPixelSize(10);
	return qRound(21
		+ QFontMetricsF(latchFont).horizontalAdvance(QStringLiteral("FILL"))
		+ 10);
}
}

// ---- VSTSlotFillCell -------------------------------------------------------

VSTSlotFillCell::VSTSlotFillCell(bool output, int slot, QWidget* parent)
	: QWidget(parent), output(output), slot(slot)
{
	setObjectName(output
		? QStringLiteral("VSTSlotFillCellOut") : QStringLiteral("VSTSlotFillCellIn"));
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::PointingHandCursor);
	setAccessibleName(output
		? tr("Output slot channel") : tr("Input slot channel"));
	refreshAccessibleValue();
}

void VSTSlotFillCell::setContent(const QString& newRole, const QString& newValue,
	bool newSilent, bool newDefaulted, bool newMissing)
{
	if (role == newRole && value == newValue && silent == newSilent
		&& defaulted == newDefaulted && missing == newMissing)
		return;
	role = newRole;
	value = newValue;
	silent = newSilent;
	defaulted = newDefaulted;
	missing = newMissing;
	refreshAccessibleValue();
	updateGeometry();
	update();
}

void VSTSlotFillCell::setChannelChoices(const QStringList& names)
{
	choices = names;
}

QStringList VSTSlotFillCell::channelChoices() const
{
	QStringList menu = choices;
	menu.append(QStringLiteral("-"));
	return menu;
}

QSize VSTSlotFillCell::sizeHint() const
{
	// The active skin measures the cell with the fonts and paddings its own
	// painter draws with, so the role, the value and the caret fit the cell
	// in each skin. (The mockup round reused the bus-selector painter and
	// they collided; until audit #348 F10 the neutral painter's fonts sized
	// the cell in every skin.)
	return SkinManager::instance()->vstSlotFillCellSize(role, value);
}

QSize VSTSlotFillCell::minimumSizeHint() const
{
	return sizeHint();
}

void VSTSlotFillCell::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	VstSlotFillCellState state;
	state.rect = rect();
	state.output = output;
	state.roleToken = role;
	state.valueText = value;
	state.silent = silent;
	state.defaulted = defaulted;
	state.missingChannel = missing;
	state.enabled = isEnabled();
	state.hovered = hovered;
	state.pressed = pressed;
	state.focused = hasFocus();
	state.menuOpen = menuOpen;
	SkinManager::instance()->paintVstSlotFillCell(painter, state);
}

void VSTSlotFillCell::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mousePressEvent(event);
		return;
	}
	pressed = true;
	update();
}

void VSTSlotFillCell::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mouseReleaseEvent(event);
		return;
	}
	const bool inside = pressed && rect().contains(event->pos());
	pressed = false;
	update();
	if (inside)
		openMenu();
}

void VSTSlotFillCell::keyPressEvent(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_Space:
	case Qt::Key_Return:
	case Qt::Key_Enter:
	case Qt::Key_Down:
		openMenu();
		return;
	default:
		QWidget::keyPressEvent(event);
	}
}

void VSTSlotFillCell::enterEvent(QEnterEvent*)
{
	hovered = true;
	update();
}

void VSTSlotFillCell::leaveEvent(QEvent*)
{
	hovered = false;
	update();
}

void VSTSlotFillCell::focusInEvent(QFocusEvent* event)
{
	QWidget::focusInEvent(event);
	update();
}

void VSTSlotFillCell::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
	update();
}

void VSTSlotFillCell::openMenu()
{
	QMenu menu(this);
	for (const QString& name : choices)
	{
		QAction* action = menu.addAction(name);
		action->setCheckable(true);
		action->setChecked(name == value);
		action->setData(name);
	}
	if (!choices.isEmpty())
		menu.addSeparator();
	QAction* dash = menu.addAction(output ? tr("Discard (-)") : tr("Silence (-)"));
	dash->setCheckable(true);
	dash->setChecked(value == QStringLiteral("-"));
	dash->setData(QStringLiteral("-"));

	menuOpen = true;
	update();
	QAction* chosen = menu.exec(mapToGlobal(QPoint(0, height())));
	menuOpen = false;
	update();
	if (chosen != nullptr)
		emit picked(slot, chosen->data().toString());
}

void VSTSlotFillCell::refreshAccessibleValue()
{
	setAccessibleDescription(QStringLiteral("%1: %2").arg(role, value));
#ifndef QT_NO_ACCESSIBILITY
	QAccessibleValueChangeEvent event(this, value);
	QAccessible::updateAccessibility(&event);
#endif
}

// ---- VSTSlotFillRail -------------------------------------------------------

VSTSlotFillRail::VSTSlotFillRail(bool output, QWidget* parent)
	: QWidget(parent), output(output)
{
	setObjectName(output
		? QStringLiteral("VSTSlotFillRailOut") : QStringLiteral("VSTSlotFillRailIn"));
	if (!output)
	{
		latch = new RailLatch(this);
		latch->setObjectName(QStringLiteral("VSTSlotFillLatch"));
		// Skins' universal QWidget stylesheet rules must not paint a box over
		// the rail's own latch rendering.
		latch->setAttribute(Qt::WA_NoSystemBackground, true);
		latch->setStyleSheet(QStringLiteral("background: transparent;"));
		latch->setFocusPolicy(Qt::StrongFocus);
		latch->setCursor(Qt::PointingHandCursor);
		latch->setAccessibleName(tr("Channel fill"));
		latch->installEventFilter(this);
		latch->setVisible(false);
	}
}

void VSTSlotFillRail::setCells(const QList<CellData>& data)
{
	if (cells.size() != data.size())
	{
		for (VSTSlotFillCell* cell : cells)
			cell->deleteLater();
		cells.clear();
		for (int i = 0; i < data.size(); i++)
		{
			VSTSlotFillCell* cell = new VSTSlotFillCell(output, i, this);
			cell->setChannelChoices(choices);
			connect(cell, &VSTSlotFillCell::picked, this, &VSTSlotFillRail::slotPicked);
			cells.append(cell);
		}
	}
	for (int i = 0; i < data.size(); i++)
	{
		const CellData& d = data[i];
		cells[i]->setContent(d.role, d.value, d.silent, d.defaulted, d.missing);
	}
	relayout();
	updateGeometry();
	update();
}

void VSTSlotFillRail::setChannelChoices(const QStringList& names)
{
	choices = names;
	for (VSTSlotFillCell* cell : cells)
		cell->setChannelChoices(names);
}

void VSTSlotFillRail::setLatchVisible(bool visible)
{
	if (latch == nullptr)
		return;
	latch->setVisible(visible);
	relayout();
	update();
}

void VSTSlotFillRail::setCollapsed(bool newCollapsed)
{
	if (collapsed == newCollapsed)
		return;
	collapsed = newCollapsed;
	for (VSTSlotFillCell* cell : cells)
		cell->setVisible(!collapsed);
	relayout();
	updateGeometry();
	update();
}

QSize VSTSlotFillRail::sizeHint() const
{
	int width = 12;
	if (latch != nullptr && latch->isVisibleTo(const_cast<VSTSlotFillRail*>(this)))
		width += latchWidth() + 10;
	if (!collapsed)
	{
		for (const VSTSlotFillCell* cell : cells)
			width += cell->sizeHint().width() + 4;
	}
	return QSize(width + 8, railHeight());
}

QSize VSTSlotFillRail::minimumSizeHint() const
{
	return QSize(40, railHeight());
}

void VSTSlotFillRail::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	VstSlotFillRailState state;
	state.rect = rect();
	state.output = output;
	state.collapsed = collapsed;
	state.enabled = isEnabled();
	if (!collapsed && !cells.isEmpty())
	{
		QRect bounds;
		for (const VSTSlotFillCell* cell : cells)
			bounds = bounds.isNull() ? cell->geometry() : bounds.united(cell->geometry());
		state.cellsRect = bounds;
	}
	if (latch != nullptr && latch->isVisibleTo(this))
	{
		state.latchRect = latch->geometry();
		state.latchHovered = latchHovered;
		state.latchPressed = latchPressed;
		state.latchFocused = latch->hasFocus();
	}
	SkinManager::instance()->paintVstSlotFillRail(painter, state);
}

void VSTSlotFillRail::resizeEvent(QResizeEvent*)
{
	relayout();
}

bool VSTSlotFillRail::eventFilter(QObject* watched, QEvent* event)
{
	if (watched != latch)
		return QWidget::eventFilter(watched, event);
	switch (event->type())
	{
	case QEvent::Enter:
		latchHovered = true;
		update();
		break;
	case QEvent::Leave:
		latchHovered = false;
		update();
		break;
	case QEvent::MouseButtonPress:
		if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
		{
			latchPressed = true;
			update();
			return true;
		}
		break;
	case QEvent::MouseButtonRelease:
		if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
		{
			const bool inside = latchPressed
				&& latch->rect().contains(static_cast<QMouseEvent*>(event)->pos());
			latchPressed = false;
			update();
			if (inside)
				emit latchToggled();
			return true;
		}
		break;
	case QEvent::KeyPress:
	{
		const int key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter)
		{
			emit latchToggled();
			return true;
		}
		break;
	}
	case QEvent::FocusIn:
	case QEvent::FocusOut:
		update();
		break;
	default:
		break;
	}
	return QWidget::eventFilter(watched, event);
}

void VSTSlotFillRail::relayout()
{
	int x = 12;
	const int height = railHeight();
	if (latch != nullptr && latch->isVisibleTo(this))
	{
		const int lw = latchWidth();
		const int lh = cellHeight();
		latch->setGeometry(x, (height - lh) / 2, lw, lh);
		x += lw + 10;
	}
	if (collapsed)
		return;
	for (VSTSlotFillCell* cell : cells)
	{
		const QSize hint = cell->sizeHint();
		cell->setGeometry(x, (height - hint.height()) / 2, hint.width(), hint.height());
		x += hint.width() + 4;
	}
}

int VSTSlotFillRail::railHeight()
{
	return 26;
}
