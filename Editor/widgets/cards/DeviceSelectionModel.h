/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Selection logic behind the in-place device picker (DeviceCardEditor).
	Pure Qt Core so EditorLogicTests can exercise it: it maps a "Device:"
	parameter string onto a chip list (one chip per available endpoint, plus
	an implicit "all" flag) and serializes back to the same pattern the legacy
	change-button dialog produced - "all", or the selected devices' device
	strings joined with "; " in list order - so equivalent selections stay
	byte-identical with configs written by the old flow. Matching uses the
	shared DeviceCommand codec, the same one the engine uses.
*/

#pragma once

#include <QList>
#include <QString>

// One available endpoint, fed into the model from the Editor's device list.
struct DeviceEntry
{
	// apoInfo->getDeviceString(): the engine's match key and the token written
	// back into the "Device:" line.
	QString deviceString;
	// The device the endpoint belongs to (getDeviceName), e.g. "USB Audio CODEC".
	QString name;
	// The endpoint's own name (getConnectionName), e.g. "Speakers".
	QString connection;
	bool installed = false;
	bool isInput = false;
};

// One selectable device chip.
struct DeviceChipInfo
{
	QString name;
	QString connection;
	QString deviceString;
	bool selected = false;
	bool installed = false;
	bool isInput = false;
};

class DeviceSelectionModel
{
public:
	// Build the chip list from the line's parameters and the available device
	// list, pre-selecting the devices the engine would match for the pattern.
	void load(const QString& parameters, const QList<DeviceEntry>& devices);

	const QList<DeviceChipInfo>& chips() const;
	bool allSelected() const;

	// The chip's text, spelled the way Windows names an endpoint: the
	// connection with its device in parentheses, "Speakers (USB Audio CODEC)".
	// The connection alone ("Speakers", "Microphone") could not tell two
	// interfaces apart (#456). Either part alone when the other is empty or
	// says the same.
	static QString chipLabel(const DeviceChipInfo& chip);

	void setAllSelected(bool on);
	// Flip one chip. While "all" is on, picking a chip narrows the selection
	// to that device: "all" releases and every other chip clears (the legacy
	// dialog made the user leave the all-devices state first).
	void toggle(const QString& deviceString);

	// Canonical parameter string for the current selection ("all" wins, like
	// the legacy dialog), byte-identical with the old change-button flow.
	QString serialize() const;

private:
	QList<DeviceChipInfo> chipList;
	bool all = false;
};
