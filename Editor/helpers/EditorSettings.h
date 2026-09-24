/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDir>
#include <QSettings>
#include <QString>

#include "services/registry/RegistryPaths.h"

// The per-file preference store (row preferences, scroll offsets): one group
// per configuration file, named by perFileGroup(). Here rather than in
// MainWindow.h so the FilterTable translation units and LegacyMigration do
// not need the main window's header for it (audit #348 TD-56).
#define EDITOR_PER_FILE_REGPATH EDITOR_REGPATH L"\\file-specific"

namespace EditorSettings
{
namespace Keys
{
// The UI language the Editor's language menu picks, read by the Editor,
// DeviceSelector (QtAppBootstrap::applyUserLocale) and the preferences
// reset. Absent means "follow the system".
inline constexpr char Language[] = "language";
inline constexpr char Skin[] = "interface/skin";
inline constexpr char Dark[] = "interface/dark";
inline constexpr char LegacyRows[] = "interface/legacyRows";
inline constexpr char NativeTitleBar[] = "interface/nativeTitleBar";
inline constexpr char KnobGainRange[] = "interface/knobGainRange";
// Analysis dock view choices. Stored under EDITOR_REGPATH like every other
// preference; they briefly lived in Qt's default QSettings location, which
// "Reset all global preferences" could not reach (audit #275 TD-01) - the
// migration in MainWindow.Analysis.cpp moves old values over once.
inline constexpr char AnalysisViewMetric[] = "analysis/viewMetric";
inline constexpr char AnalysisIncludeLatency[] = "analysis/includeLatency";
}

// The group a configuration file's per-file preferences live under: its
// native path with every backslash as '|', because a registry key name cannot
// hold a backslash. One spelling for the writer, the reader and the legacy
// migration that moves a file's preferences to its new path.
inline QString perFileGroup(const QString& configPath)
{
	return QDir::toNativeSeparators(configPath).replace(QLatin1Char('\\'), QLatin1Char('|'));
}

struct SkinChoice
{
	QString id;
	bool dark = false;
};

inline SkinChoice readSkinChoice(const QSettings& settings, bool defaultDark)
{
	return {
		settings.value(QLatin1String(Keys::Skin), QStringLiteral("studio")).toString(),
		settings.value(QLatin1String(Keys::Dark), defaultDark).toBool()
	};
}

inline void writeSkinChoice(QSettings& settings, const SkinChoice& choice)
{
	settings.setValue(QLatin1String(Keys::Skin), choice.id);
	settings.setValue(QLatin1String(Keys::Dark), choice.dark);
}
}
