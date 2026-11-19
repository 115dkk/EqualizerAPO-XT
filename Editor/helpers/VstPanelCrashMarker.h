/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

class QSettings;

// Records the libraries whose native plug-in panels are open. A record left by
// an abnormal exit is consumed once when that library's row is restored.
class VstPanelCrashMarker
{
public:
	static void markOpen(const QString& libraryPath);
	static void markClosed(const QString& libraryPath);
	static bool takeLeftover(const QString& libraryPath);

	// QSettings overloads let EditorLogicTests use an isolated INI store.
	static void markOpen(QSettings& settings, const QString& libraryPath);
	static void markClosed(QSettings& settings, const QString& libraryPath);
	static bool takeLeftover(QSettings& settings, const QString& libraryPath);

private:
	static QString normalizedIdentity(const QString& libraryPath);
};
