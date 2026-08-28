/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

class QCheckBox;
class QLabel;
class QToolBar;
class SkinComboBox;

namespace MainToolbarKit
{
struct Content
{
	QString instantMode;
	QString saved;
	QString device;
	QString channels;
	QString deviceValue;
	QString channelValue;
	QString formatText;
	QString formatSeverity = QStringLiteral("normal");
	QString instantModeToolTip;
	QString savedToolTip;
	QString formatToolTip;
	bool formatVisible = false;
};

struct Widgets
{
	QCheckBox* instantMode = nullptr;
	QLabel* dirtyStatus = nullptr;
	SkinComboBox* device = nullptr;
	QLabel* deviceFormat = nullptr;
	SkinComboBox* channels = nullptr;
};

// Populates the shared action/widget train. The real toolbar already owns its
// Designer actions; offscreen replicas request the same standard actions here.
Widgets populate(QToolBar* toolBar, const Content& content, bool addStandardActions);

// Items whose visibility communicates state rather than toolbar health.
const QStringList& visibilityIsDataObjectNames();
}
