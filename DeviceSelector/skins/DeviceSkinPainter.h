/*
	This file is part of EqualizerAPO-XT.
	Copyright (C) 2026 EqualizerAPO-XT contributors
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Form layer of the Device Selector's skin identity. The dialog's device
	list, buttons and the troubleshooting disclosure delegate every pixel to
	the painter matching the Editor's active skin; QSS remains a colour coat
	on the stock sub-widgets only (combo box, check boxes, scroll bars).

	One painter per skin, resolved by id through forSkin(). The widgets own
	input and state (hover progress is animated by the widgets and handed in
	pre-mixed); painting is a pure function of state + tokens, so a theme
	switch restyles everything with a repaint.
*/

#pragma once

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QString>

#include "Editor/skins/SkinThemeData.h"

// A device row (or a section header) as the list delegate sees it.
struct DeviceRowState
{
	QString connection;    // "Speakers", "CABLE Input", ...
	QString device;        // "VB-Audio Virtual Cable", ...
	QString state;         // localized status sentence ("APO will be installed, Default device")
	bool section = false;  // true for the "Playback devices" / "Capture devices" rows
	bool expanded = true;  // sections only: children visible
	bool input = false;    // capture side (sections and their devices)
	bool checked = false;  // APO selected for install
	bool installed = false;
	bool experimental = false;
	bool defaultDevice = false;
	bool unavailable = false; // disabled or unplugged endpoint
	bool selected = false;    // list selection (troubleshooting target)
	bool pressed = false;     // toggle hit area held down
	double hover = 0.0;       // animated 0..1
	int index = 0;            // row number under its section (zebra striping, unit numbers)
};

// One of the dialog's push buttons (OK / Cancel).
struct DeviceButtonState
{
	QString text;
	bool primary = false; // the accept button
	bool enabled = true;
	bool pressed = false;
	bool focused = false;
	double hover = 0.0; // animated 0..1
};

// The troubleshooting disclosure header.
struct DeviceDisclosureState
{
	QString title;
	bool open = false;
	double hover = 0.0; // animated 0..1
};

class DeviceSkinPainter
{
public:
	virtual ~DeviceSkinPainter() = default;

	// Painter for a resolved or unresolved skin id (SkinThemeData::resolveId
	// aliases apply). Returns process-lifetime singletons, never null.
	static const DeviceSkinPainter* forSkin(const QString& skinId);

	// The dialog-wide theme, set once by main() when it dresses the process
	// (and per iteration by the preview shot mode). Widgets read these so
	// promoted .ui widgets need no constructor arguments.
	static void setActiveTheme(const QString& skinId, bool dark);
	// The Editor's heritage (legacyRows) escape hatch: neutral base forms in
	// classic light system colours instead of a skin instrument.
	static void setHeritageTheme();
	static const DeviceSkinPainter* active();
	static const SkinTokens& activeTokens();

	virtual int rowHeight(const QFontMetrics& fm, bool section) const;
	// The toggle's hit area inside a device row; the delegate consumes
	// clicks here as check toggles instead of selection changes. At least
	// 40px wide - the whole left end of the row belongs to the toggle.
	virtual QRect toggleRect(const QRect& rowRect) const;
	// Everything visible for one row: background, toggle, names, status.
	virtual void paintRow(QPainter& painter, const QRect& rect, const DeviceRowState& state, const SkinTokens& tokens) const;

	virtual QSize buttonSizeHint(const QFontMetrics& fm, const QString& text) const;
	virtual void paintButton(QPainter& painter, const QRect& rect, const DeviceButtonState& state, const SkinTokens& tokens) const;

	virtual void paintDisclosure(QPainter& painter, const QRect& rect, const DeviceDisclosureState& state, const SkinTokens& tokens) const;
};
