/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	In-place device picker for "Device:" card rows. Replaces the read-only
	device table plus modal change-dialog with checkable chips directly in the
	card body: one chip per endpoint (installed devices and the current
	selection shown by default, the rest behind a reveal toggle), plus an "All
	devices" chip. Selection state and serialization live in
	DeviceSelectionModel; the written line goes through the shared DeviceCommand
	codec and stays byte-identical with what the legacy dialog produced. The
	chips are plain checkable QToolButtons (objectName "DeviceChip"), so each
	skin's QToolButton styling dresses them instead of the native tree.
*/

#pragma once

#include "Editor/IFilterGUI.h"
#include "DeviceSelectionModel.h"

class FilterTable;
class FlowLayout;
class QToolButton;

class DeviceCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	explicit DeviceCardEditor(FilterTable* filterTable, const QString& parameters, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void allToggled(bool checked);
	void showAllToggled(bool checked);

private:
	void reloadChips();
	// Re-apply the model's states to the existing chips (no rebuild, so it
	// is safe inside a chip's own toggled signal).
	void syncChipStates();

	DeviceSelectionModel model;
	FlowLayout* flow = nullptr;
	QToolButton* allChip = nullptr;
	QToolButton* showAllButton = nullptr;
	int hiddenUninstalled = 0;
	bool showAll = false;
	bool updating = false;
};
