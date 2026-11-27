/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <optional>

#include <QStringList>

#include "services/settings/EditorSettings.h"

class MainWindow;
class QCommandLineParser;

namespace EditorGates
{
std::optional<int> runHeadless(const QStringList& arguments);
// Engaged only while a headless runner is active; cards when no modifier was
// given. A headless window must never restore the user's saved presentation.
std::optional<EditorSettings::ListMode> headlessListMode();
// Log the cards fallback once per skin, not once per table or theme.
void reportListModeFallback(const QString& skinId);
bool wantsTestWindow(const QStringList& arguments);
void registerOptions(QCommandLineParser& parser);
bool arm(MainWindow& window, const QCommandLineParser& parser);
QStringList flags();
}
