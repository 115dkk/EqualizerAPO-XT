/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
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
#include <QFont>
#include <QFontMetrics>
#include <QMargins>
#include <QVector>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QString>

#include "Editor/skins/SkinThemeData.h"

// What the installation check found on a device. None everywhere but in the
// check's own list (DeviceTestView), where the row reports instead of offering
// a choice.
enum class DeviceReport
{
	None,
	Waiting,    // the check has not answered for this device yet
	Alive,      // the APO came up
	Degraded,   // the APO came up, the original APO beside it did not
	Dead,       // the APO did not come up
	Untestable, // disabled or unplugged: nothing could be asked
	Unchecked   // the check stopped before it reached this device
};

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
	bool defaultDevice = false;
	bool unavailable = false; // disabled or unplugged endpoint
	bool selected = false;    // list selection (troubleshooting target)
	bool pressed = false;     // toggle hit area held down
	double hover = 0.0;       // animated 0..1
	int index = 0;            // row number under its section (zebra striping, unit numbers)
	DeviceReport report = DeviceReport::None;
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

// A block of sentences the dialog has to say: the check's verdict, or what
// went wrong on one device and what to do about it. The skin gives it a
// ground, a tone mark and its own type, so a paragraph is a part of the
// instrument and not text left lying on the window.
struct DevicePanelState
{
	enum class LineKind
	{
		Body,   // what happened
		Action, // what the user can do; carries the skin's action mark
		System  // a message quoted from Windows, set apart in the mono face
	};
	struct Line
	{
		LineKind kind = LineKind::Body;
		QString text;
	};

	DeviceReport tone = DeviceReport::None;
	QString title;
	QVector<Line> lines;
};

// How a skin sets a panel's text. The flow (title, then each line under the
// last, wrapped to the panel) is shared; the faces, inks and insets are the
// skin's.
struct DevicePanelStyle
{
	QFont titleFont;
	QFont bodyFont;
	QFont monoFont;
	QColor titleInk;
	QColor bodyInk;
	QColor mutedInk;
	QColor toneInk;
	QMargins padding = QMargins(14, 10, 14, 10);
	int lineGap = 3;
	int actionIndent = 16;
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
	// The active skin's roster id, empty in the heritage look.
	static const QString& activeSkinId();

	// The status sentence's ink while a row reports a check: red for a dead
	// APO, the warning colour for a degraded one, the skin's pending ink while
	// waiting. False leaves the skin's own choice (and always for None).
	static bool reportInk(const DeviceRowState& state, const SkinTokens& tokens, QColor& ink);

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

	// The colour a tone speaks in, and its short instrument code (RUN, OK,
	// WARN, FAIL, N/A) for the skins that label their parts.
	static QColor toneColor(DeviceReport tone, const SkinTokens& tokens);
	static QString toneCode(DeviceReport tone);

	// A panel: the skin supplies the style, the ground (with its tone mark)
	// and the mark in front of an action line; the text flow is shared.
	virtual DevicePanelStyle panelStyle(const DevicePanelState& state, const QFont& base, const SkinTokens& tokens) const;
	virtual void paintPanelGround(QPainter& painter, const QRect& rect, const DevicePanelState& state, const SkinTokens& tokens) const;
	virtual void paintActionMark(QPainter& painter, const QRectF& cell, const QColor& ink, const SkinTokens& tokens) const;
	virtual void paintSystemWell(QPainter& painter, const QRectF& rect, const SkinTokens& tokens) const;
	int panelHeight(int width, const DevicePanelState& state, const QFont& base, const SkinTokens& tokens) const;
	void paintPanel(QPainter& painter, const QRect& rect, const DevicePanelState& state, const QFont& base, const SkinTokens& tokens) const;

private:
	int flowPanel(QPainter* painter, const QRect& rect, const DevicePanelState& state, const QFont& base, const SkinTokens& tokens) const;

public:
};
