/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDir>
#include <QList>
#include <QSettings>
#include <QString>
#include <optional>

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
// The filter list's presentation, as a word (see ListMode).
inline constexpr char ListMode[] = "interface/listMode";
// Up to v2.85 the same choice as a boolean. Read once when ListMode is
// absent; writeListMode removes it.
inline constexpr char RetiredLegacyRows[] = "interface/legacyRows";
inline constexpr char NativeTitleBar[] = "interface/nativeTitleBar";
// Off by default: file dialogs are the skinned widget-based ones. On, every
// file dialog is the Windows one (issue #508).
inline constexpr char NativeFileDialog[] = "interface/nativeFileDialog";
inline constexpr char KnobGainRange[] = "interface/knobGainRange";
// Analysis dock view choices. Stored under EDITOR_REGPATH like every other
// preference; they briefly lived in Qt's default QSettings location, which
// "Reset all global preferences" could not reach (audit #275 TD-01) - the
// migration in MainWindow.Analysis.cpp moves old values over once.
inline constexpr char AnalysisViewMetric[] = "analysis/viewMetric";
inline constexpr char AnalysisIncludeLatency[] = "analysis/includeLatency";
inline constexpr char GraphDockPosition[] = "interface/graphDockPosition";
inline constexpr char Geometry[] = "geometry";
inline constexpr char WindowState[] = "windowState";
inline constexpr char InstantMode[] = "instantMode";
inline constexpr char AutomaticUpdates[] = "update/automatic";
inline constexpr char SelectedDevice[] = "selectedDevice";
inline constexpr char SelectedDeviceInput[] = "selectedDeviceInput";
inline constexpr char SelectedChannelMask[] = "selectedChannelMask";
inline constexpr char AnalysisStartFrom[] = "analysis/startFrom";
inline constexpr char AnalysisChannel[] = "analysis/channel";
inline constexpr char AnalysisResolution[] = "analysis/resolution";
inline constexpr char OpenFiles[] = "openFiles";
inline constexpr char TabIndex[] = "tabIndex";
inline constexpr char RecentFiles[] = "recentFiles";
}

// How the filter list is presented. Stored as a word, so a mode added later
// never renumbers a stored value.
enum class ListMode
{
	Cards,        // "cards"
	Rows,         // "rows"
	LegacyEditor  // "legacy"
};

// The stored word of a mode, and back. An unknown word gives nullopt.
inline QString listModeWord(ListMode mode)
{
	switch (mode)
	{
	case ListMode::Cards:
		return QStringLiteral("cards");
	case ListMode::Rows:
		return QStringLiteral("rows");
	case ListMode::LegacyEditor:
		return QStringLiteral("legacy");
	}
	return QString();
}

inline std::optional<ListMode> listModeFromWord(const QString& word)
{
	if (word == QLatin1String("cards"))
		return ListMode::Cards;
	if (word == QLatin1String("rows"))
		return ListMode::Rows;
	if (word == QLatin1String("legacy"))
		return ListMode::LegacyEditor;
	return std::nullopt;
}

// The saved presentation. The word under Keys::ListMode wins when it names a
// mode this build knows; a word it does not know (written by a newer build)
// reads as Cards. Without that key the retired boolean decides, and a
// registry with neither key reads as Cards.
inline ListMode readListMode(const QSettings& settings)
{
	if (settings.contains(QLatin1String(Keys::ListMode)))
		return listModeFromWord(settings.value(QLatin1String(Keys::ListMode)).toString()).value_or(ListMode::Cards);
	return settings.value(QLatin1String(Keys::RetiredLegacyRows), false).toBool()
		? ListMode::LegacyEditor : ListMode::Cards;
}

// Stores the word and removes the retired boolean.
inline void writeListMode(QSettings& settings, ListMode mode)
{
	settings.setValue(QLatin1String(Keys::ListMode), listModeWord(mode));
	settings.remove(QLatin1String(Keys::RetiredLegacyRows));
}

// The presentation the Editor starts in, decided once for a store that has
// none saved. A store with the word or the retired boolean reads as
// readListMode says and is left as it is. Without either, any other value of
// the interface group shows someone who ran a skinned build before the word
// existed (the Editor saves interface/skin every time it closes): they keep
// cards, which is what they had. A store with no interface value at all is a
// new installation and starts in Modern rows. The store existing proves
// nothing: it is the key the original Equalizer APO's editor keeps its window
// geometry in, so someone moving over from it is a new installation too.
// The answer is written down, so the question is asked once per machine.
// Only the Editor's interactive start calls this; DeviceSelector and the
// headless checks read with readListMode and never decide.
inline ListMode adoptListMode(QSettings& settings)
{
	if (settings.contains(QLatin1String(Keys::ListMode)) || settings.contains(QLatin1String(Keys::RetiredLegacyRows)))
		return readListMode(settings);

	settings.beginGroup(QStringLiteral("interface"));
	const bool ranSkinnedBuildBefore = !settings.childKeys().isEmpty();
	settings.endGroup();

	const ListMode mode = ranSkinnedBuildBefore ? ListMode::Cards : ListMode::Rows;
	writeListMode(settings, mode);
	return mode;
}

// The group a configuration file's per-file preferences live under: its
// native path with every backslash as '|', because a registry key name cannot
// hold a backslash. One spelling for the writer, the reader and the legacy
// migration that moves a file's preferences to its new path.
inline QString perFileGroup(const QString& configPath)
{
	return QDir::toNativeSeparators(configPath).replace(QLatin1Char('\\'), QLatin1Char('|'));
}

struct DeviceChoice
{
	QString guid;
	QString name;
	bool input = false;
};

inline void writeSelectedDevice(QSettings& settings, const DeviceChoice& choice)
{
	// Voicemeeter strips have no endpoint GUID. Keep their name identity;
	// ASIO playback and capture share a CLSID, so retain direction separately.
	settings.setValue(QLatin1String(Keys::SelectedDevice), choice.guid.isEmpty() ? choice.name : choice.guid);
	settings.setValue(QLatin1String(Keys::SelectedDeviceInput), choice.input);
}

inline int readSelectedDevice(QSettings& settings, const QList<DeviceChoice>& choices)
{
	const QString saved = settings.value(QLatin1String(Keys::SelectedDevice)).toString();
	if (saved.isEmpty())
		return -1;
	const QVariant input = settings.value(QLatin1String(Keys::SelectedDeviceInput));
	bool guidMatched = false;
	for (int i = 0; i < choices.size(); i++)
	{
		if (!choices[i].guid.isEmpty() && choices[i].guid.compare(saved, Qt::CaseInsensitive) == 0)
		{
			guidMatched = true;
			if (!input.isValid() || choices[i].input == input.toBool())
				return i;
		}
	}
	if (guidMatched)
		return -1;
	// A pre-GUID preference contains getDeviceString(), including its connection
	// name. Migrate only an actual match (audit #467 TD-50).
	for (int i = 0; i < choices.size(); i++)
	{
		if (!choices[i].name.isEmpty() && choices[i].name.compare(saved, Qt::CaseInsensitive) == 0)
		{
			writeSelectedDevice(settings, choices[i]);
			return i;
		}
	}
	return -1;
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

inline bool readAutomaticUpdates(const QSettings& settings)
{
	const QVariant saved = settings.value(QLatin1String(Keys::AutomaticUpdates));
	if (!saved.isValid())
		return true;

	const QString text = saved.toString().trimmed();
	if (text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0 || text == QLatin1String("0"))
		return false;
	if (text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || text == QLatin1String("1"))
		return true;
	return true;
}

inline void writeAutomaticUpdates(QSettings& settings, bool enabled)
{
	settings.setValue(QLatin1String(Keys::AutomaticUpdates), enabled);
}
}
