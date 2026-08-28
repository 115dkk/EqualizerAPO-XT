/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Selection logic behind the in-place channel multi-select editor
	(ChannelCardEditor). Pure Qt Core so EditorLogicTests can exercise it:
	it maps a "Channel:" parameter string onto a chip list and serializes
	back through ChannelCommand (the grammar owner) with the same canonical
	ordering the legacy multi-select dialog produced, so equivalent
	selections stay byte-identical with configs written by the old flow.
*/

#pragma once

#include <QList>
#include <QString>
#include <string>
#include <vector>

// One selectable channel chip.
struct ChannelChip
{
	// Upper-cased selector token, used for display and serialization.
	QString name;
	bool selected = false;
	// False for custom/virtual channels that are named in the line (or added
	// by the user) but not part of the device's channel set.
	bool fromDevice = false;
};

class ChannelSelectionModel
{
public:
	// Build the chip list from the line's parameters and the device channel
	// names (ChannelLayout::getChannelNames order; may be empty when no
	// device is selected). Device chips come first in the legacy dialog's
	// canonical order, custom tokens follow in the order they were written.
	void load(const QString& parameters, const std::vector<std::wstring>& deviceChannels);

	const QList<ChannelChip>& chips() const;
	bool allSelected() const;

	void setAllSelected(bool on);
	// Flip one chip. While ALL is on, picking a chip narrows the selection
	// to that chip: ALL releases and every other chip clears (the legacy
	// dialog made the user uncheck ALL first before any seat would take a
	// click).
	void toggle(const QString& name);
	// Select an existing chip matching the token (names, aliases or position
	// numbers) or append a new custom chip. Returns false when the token is
	// empty or not a single selector. Narrows from ALL like toggle().
	bool addCustom(const QString& name);

	// Canonical parameter string for the current selection ("ALL" wins, like
	// the legacy dialog). Serialization goes through ChannelCommand.
	QString serialize() const;

private:
	int chipIndex(const QString& name) const;
	int resolveDeviceChip(const QString& token) const;
	// ALL -> one explicit seat: release ALL and clear every chip so the
	// caller's pick becomes the whole selection.
	void narrowFromAll();

	QList<ChannelChip> chipList;
	QList<QString> deviceNames;
	bool all = false;
};
