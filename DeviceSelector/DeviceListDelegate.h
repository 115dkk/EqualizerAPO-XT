/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Renders the device tree through the active skin's DeviceSkinPainter and
	owns the list's feel: per-row hover progress (interruptible 150ms in /
	110ms out), toggle press feedback, click-to-toggle on the painted jack,
	double-click-to-toggle anywhere on a device row and single-click section
	folding. The QTreeWidget keeps all data and behaviour; this delegate only
	replaces its pixels and pointer feel.
*/

#pragma once

#include <QHash>
#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

class QTreeWidget;
class QVariantAnimation;

class DeviceListDelegate : public QStyledItemDelegate
{
	Q_OBJECT

public:
	// Data roles the dialog stores on each device item (column 0).
	enum Roles
	{
		DeviceNameRole = Qt::UserRole + 1,
		StateTextRole,
		InstalledRole,
		DefaultDeviceRole,
		UnavailableRole,
		InputSideRole
	};

	explicit DeviceListDelegate(QTreeWidget* tree);

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
	bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;

	// The preview shots force a hovered row without a real cursor.
	void setForcedHover(const QModelIndex& index);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	double hoverProgress(const QModelIndex& index) const;
	void animateHover(const QPersistentModelIndex& index, double target, int duration);
	void setHoveredIndex(const QModelIndex& index);
	void repaintRow(const QPersistentModelIndex& index);

	QTreeWidget* tree;
	QPersistentModelIndex hoveredIndex;
	QPersistentModelIndex pressedIndex;
	QPersistentModelIndex forcedHover;
	QHash<QPersistentModelIndex, double> hoverValues;
	QHash<QPersistentModelIndex, QVariantAnimation*> hoverAnimations;
};
