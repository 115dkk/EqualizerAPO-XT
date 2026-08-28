/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QSettings>
#include <QString>

namespace EditorSettings
{
namespace Keys
{
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
